#include "gfx/SpirvCompiler.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <stdexcept>
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
        auto result = std::make_shared<SpirvModule>();
        if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment) {
            result->error = "Unsupported shader stage";
            return result;
        }
        if (source.empty() || source.size() > std::size_t{4} * 1024 * 1024 || source.find('\0') != std::string_view::npos) {
            result->error = "Invalid or oversized shader source";
            return result;
        }
        const auto key = std::pair{stage, std::string(source)};
        if (const auto found = m_cache.find(key); found != m_cache.end())
            return found->second;
        if (!available()) {
            result->error = "glslangValidator and spirv-val were not found or are no longer executable";
            return result;
        }
#if defined(WEMU_RUNTIME_GLSLANG) && defined(WEMU_RUNTIME_SPIRV_VAL)
        try {
            TemporaryDirectory temporary;
            const auto input = temporary.path / (stage == ShaderStage::Vertex ? "shader.vert" : "shader.frag");
            const auto output = temporary.path / "shader.spv";
            const auto log = temporary.path / "tool.log";
            {
                std::ofstream file(input, std::ios::binary);
                file.write(source.data(), static_cast<std::streamsize>(source.size()));
                file.close();
                if (!file)
                    throw std::runtime_error("Cannot write shader source");
            }
            execute({WEMU_RUNTIME_GLSLANG, "-V", "--target-env", "vulkan1.0", "-o", output.string(), input.string()}, log);
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
            const auto bytesCached = key.second.size() + result->words.size() * sizeof(std::uint32_t);
            if (m_cache.size() >= 128 || m_cacheBytes + bytesCached > std::size_t{32} * 1024 * 1024) {
                m_cache.clear();
                m_cacheBytes = 0;
            }
            m_cache.emplace(key, result);
            m_cacheBytes += bytesCached;
        } catch (const std::exception &error) {
            result->words.clear();
            result->error = error.what();
        }
#endif
        return result;
    }
} // namespace Core::Gfx
