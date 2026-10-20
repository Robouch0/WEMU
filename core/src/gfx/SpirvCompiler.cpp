#include "gfx/SpirvCompiler.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace Core::Gfx {
    namespace {
        struct TemporaryDirectory {
                std::filesystem::path path;
                TemporaryDirectory()
                {
                    auto pattern = (std::filesystem::temp_directory_path() / "wemu-spirv-XXXXXX").string();
                    if (!mkdtemp(pattern.data()))
                        throw std::runtime_error("Cannot create private shader compilation directory");
                    path = pattern;
                }
                ~TemporaryDirectory()
                {
                    std::error_code error;
                    std::filesystem::remove_all(path, error);
                }
        };

        std::string diagnostics(const std::filesystem::path &path)
        {
            std::ifstream log(path, std::ios::binary);
            std::array<char, 8192> data{};
            log.read(data.data(), data.size());
            return std::string(data.data(), static_cast<std::size_t>(log.gcount()));
        }

        void execute(std::vector<std::string> arguments, const std::filesystem::path &log)
        {
            struct Actions {
                    posix_spawn_file_actions_t value;
                    Actions()
                    {
                        if (posix_spawn_file_actions_init(&value))
                            throw std::runtime_error("Cannot initialize shader compiler subprocess");
                    }
                    ~Actions() { posix_spawn_file_actions_destroy(&value); }
            } actions;
            const auto checkAction = [](int error) {
                if (error)
                    throw std::runtime_error(std::string("Cannot redirect shader compiler: ") + std::strerror(error));
            };
            checkAction(posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
            checkAction(posix_spawn_file_actions_addopen(&actions.value, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
            checkAction(posix_spawn_file_actions_adddup2(&actions.value, STDOUT_FILENO, STDERR_FILENO));
            std::vector<char *> argv;
            argv.reserve(arguments.size() + 1);
            for (auto &argument: arguments)
                argv.push_back(argument.data());
            argv.push_back(nullptr);
            pid_t child{};
            const auto spawned = posix_spawn(&child, argv.front(), &actions.value, nullptr, argv.data(), environ);
            if (spawned)
                throw std::runtime_error(std::string("Cannot launch shader tool: ") + std::strerror(spawned));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            int status{};
            for (;;) {
                const auto waited = waitpid(child, &status, WNOHANG);
                if (waited == child)
                    break;
                if (waited < 0 && errno != EINTR) {
                    const auto error = errno;
                    if (error != ECHILD) {
                        kill(child, SIGKILL);
                        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
                        }
                    }
                    throw std::runtime_error(std::string("Cannot reap shader tool: ") + std::strerror(error));
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    kill(child, SIGKILL);
                    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
                    }
                    throw std::runtime_error("Shader tool exceeded 15-second deadline");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!WIFEXITED(status) || WEXITSTATUS(status))
                throw std::runtime_error("Shader tool failed: " + diagnostics(log));
        }
    } // namespace

    namespace {
        constexpr char kCacheMagic[8]{'W', 'E', 'M', 'U', 'S', 'P', 'V', '1'};
        constexpr std::uint64_t kMaxCachedWords = std::uint64_t{4} * 1024 * 1024;

        std::atomic<std::uint64_t> g_nextModuleId{1};

        struct Descriptor {
                int fd;
                ~Descriptor()
                {
                    if (fd >= 0)
                        ::close(fd);
                }
        };
        bool privateOwner(const struct stat &info) { return info.st_uid == ::geteuid() && !(info.st_mode & 0077); }

        std::string cacheFilename(ShaderStage stage, std::string_view source)
        {
            std::uint64_t hash = 0xCBF29CE484222325ull ^ std::uint64_t(stage); // FNV-1a
            for (const unsigned char c: source)
                hash = (hash ^ c) * 0x100000001B3ull;
            constexpr char hex[] = "0123456789abcdef";
            std::string name;
            for (unsigned i = 0; i < 16; ++i, hash >>= 4)
                name += hex[hash & 15];
            return name + ".spv";
        }
        bool readAll(int fd, void *data, std::size_t bytes)
        {
            auto *out = static_cast<char *>(data);
            while (bytes) {
                const auto n = ::read(fd, out, bytes);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    return false;
                out += n;
                bytes -= std::size_t(n);
            }
            return true;
        }
        bool writeAll(int fd, const void *data, std::size_t bytes)
        {
            const auto *in = static_cast<const char *>(data);
            while (bytes) {
                const auto n = ::write(fd, in, bytes);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    return false;
                in += n;
                bytes -= std::size_t(n);
            }
            return true;
        }
    } // namespace

    SpirvCompiler::SpirvCompiler(const std::string &cacheDirectory)
    {
        if (cacheDirectory.empty())
            return;
        // Create missing parents (e.g. ~/.cache/wemu); only the leaf must be private.
        for (auto slash = cacheDirectory.find('/', 1); slash != std::string::npos; slash = cacheDirectory.find('/', slash + 1))
            ::mkdir(cacheDirectory.substr(0, slash).c_str(), 0700);
        if (::mkdir(cacheDirectory.c_str(), 0700) && errno != EEXIST)
            return;
        Descriptor dir{::open(cacheDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        struct stat info{};
        if (dir.fd < 0 || ::fstat(dir.fd, &info) || !S_ISDIR(info.st_mode) || !privateOwner(info))
            return;
        m_directory = dir.fd;
        dir.fd = -1;
    }

    SpirvCompiler::~SpirvCompiler()
    {
        if (m_directory >= 0)
            ::close(m_directory);
    }

    std::string SpirvCompiler::defaultCacheDirectory()
    {
        if (const char *configured = std::getenv("WEMU_SHADER_CACHE_DIR"))
            return configured;
        if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache && *cache == '/')
            return std::string(cache) + "/wemu/spirv";
        if (const char *home = std::getenv("HOME"); home && *home == '/')
            return std::string(home) + "/.cache/wemu/spirv";
        return {};
    }

    std::shared_ptr<SpirvModule> SpirvCompiler::loadCached(ShaderStage stage, std::string_view source) const
    {
        if (m_directory < 0)
            return nullptr;
        Descriptor file{::openat(m_directory, cacheFilename(stage, source).c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        struct stat info{};
        if (file.fd < 0 || ::fstat(file.fd, &info) || !S_ISREG(info.st_mode) || !privateOwner(info) || info.st_nlink != 1)
            return nullptr;
        char magic[sizeof(kCacheMagic)]{};
        std::uint32_t storedStage{};
        std::uint64_t sourceBytes{}, wordCount{};
        if (!readAll(file.fd, magic, sizeof(magic)) || !std::equal(std::begin(magic), std::end(magic), kCacheMagic) ||
            !readAll(file.fd, &storedStage, sizeof(storedStage)) || storedStage != std::uint32_t(stage) ||
            !readAll(file.fd, &sourceBytes, sizeof(sourceBytes)) || sourceBytes != source.size() ||
            !readAll(file.fd, &wordCount, sizeof(wordCount)) || wordCount < 5 || wordCount > kMaxCachedWords ||
            std::uint64_t(info.st_size) !=
                    sizeof(magic) + sizeof(storedStage) + sizeof(sourceBytes) + sizeof(wordCount) + sourceBytes + wordCount * 4)
            return nullptr;
        std::string stored(sourceBytes, '\0');
        if (!readAll(file.fd, stored.data(), stored.size()) || stored != source)
            return nullptr;
        auto module = std::make_shared<SpirvModule>();
        module->words.resize(wordCount);
        if (!readAll(file.fd, module->words.data(), wordCount * 4) || module->words[0] != 0x07230203u)
            return nullptr;
        module->id = g_nextModuleId++;
        return module;
    }

    void SpirvCompiler::storeCached(ShaderStage stage, std::string_view source, const SpirvModule &module) const
    {
        if (m_directory < 0)
            return;
        static std::atomic<std::uint64_t> sequence{};
        const auto temporary = ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(sequence++);
        Descriptor file{::openat(m_directory, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (file.fd < 0)
            return;
        const std::uint32_t storedStage = std::uint32_t(stage);
        const std::uint64_t sourceBytes = source.size(), wordCount = module.words.size();
        const bool written = writeAll(file.fd, kCacheMagic, sizeof(kCacheMagic)) && writeAll(file.fd, &storedStage, sizeof(storedStage)) &&
                             writeAll(file.fd, &sourceBytes, sizeof(sourceBytes)) && writeAll(file.fd, &wordCount, sizeof(wordCount)) &&
                             writeAll(file.fd, source.data(), source.size()) && writeAll(file.fd, module.words.data(), module.words.size() * 4);
        if (!written || ::renameat(m_directory, temporary.c_str(), m_directory, cacheFilename(stage, source).c_str()))
            ::unlinkat(m_directory, temporary.c_str(), 0);
    }

    bool SpirvCompiler::available() noexcept
    {
#if defined(WEMU_RUNTIME_GLSLANG) && defined(WEMU_RUNTIME_SPIRV_VAL)
        return access(WEMU_RUNTIME_GLSLANG, X_OK) == 0 && access(WEMU_RUNTIME_SPIRV_VAL, X_OK) == 0;
#else
        return false;
#endif
    }

    std::shared_ptr<const SpirvModule> SpirvCompiler::compile(ShaderStage stage, std::string_view source)
    {
        if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment && stage != ShaderStage::Compute) {
            auto result = std::make_shared<SpirvModule>();
            result->error = "Unsupported shader stage";
            return result;
        }
        // Hot path: every native draw looks its shaders up here, so a hit hashes the source once
        // and allocates nothing.
        auto &cache = m_cache[std::size_t(stage)];
        if (const auto found = cache.find(source); found != cache.end())
            return found->second;
        auto result = std::make_shared<SpirvModule>();
        if (source.empty() || source.size() > std::size_t{4} * 1024 * 1024 || source.find('\0') != std::string_view::npos) {
            result->error = "Invalid or oversized shader source";
            return result;
        }
        const auto remember = [&](std::shared_ptr<const SpirvModule> module) {
            const auto bytesCached = source.size() + module->words.size() * sizeof(std::uint32_t);
            if (cachedModules() >= 4096 || m_cacheBytes + bytesCached > std::size_t{256} * 1024 * 1024) {
                for (auto &stageCache: m_cache)
                    stageCache.clear();
                m_cacheBytes = 0;
            }
            cache.emplace(std::string(source), module);
            m_cacheBytes += bytesCached;
            return module;
        };
        if (auto cached = loadCached(stage, source)) {
            ++m_diskHits;
            return remember(std::move(cached));
        }
        if (!available()) {
            result->error = "glslangValidator and spirv-val were not found or are no longer executable";
            return result;
        }
#if defined(WEMU_RUNTIME_GLSLANG) && defined(WEMU_RUNTIME_SPIRV_VAL)
        try {
            TemporaryDirectory temporary;
            const auto input = temporary.path / (stage == ShaderStage::Vertex     ? "shader.vert"
                                                 : stage == ShaderStage::Fragment ? "shader.frag"
                                                                                  : "shader.comp");
            const auto output = temporary.path / "shader.spv";
            const auto log = temporary.path / "tool.log";
            {
                std::ofstream file(input, std::ios::binary);
                file.write(source.data(), static_cast<std::streamsize>(source.size()));
                file.close();
                if (!file)
                    throw std::runtime_error("Cannot write shader source");
            }
            execute({WEMU_RUNTIME_GLSLANG, "-V", "-Os", "--target-env", "vulkan1.0", "-o", output.string(), input.string()}, log);
            execute({WEMU_RUNTIME_SPIRV_VAL, "--target-env", "vulkan1.0", output.string()}, log);
            std::ifstream file(output, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error("Cannot read compiled shader");
            const auto bytes = file.tellg();
            if (bytes < 20 || bytes > std::streamoff{16} * 1024 * 1024 || bytes % 4)
                throw std::runtime_error("Invalid SPIR-V output size");
            result->words.resize(static_cast<std::size_t>(bytes) / 4);
            file.seekg(0);
            file.read(reinterpret_cast<char *>(result->words.data()), bytes);
            if (!file || result->words[0] != 0x07230203u)
                throw std::runtime_error("Invalid SPIR-V output");
            result->id = g_nextModuleId++;
            storeCached(stage, source, *result);
            return remember(result);
        } catch (const std::exception &error) {
            result->words.clear();
            result->error = error.what();
        }
#endif
        return result;
    }
} // namespace Core::Gfx
