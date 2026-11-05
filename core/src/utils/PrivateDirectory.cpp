#include "utils/PrivateDirectory.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Utils {
    namespace {
        struct Descriptor {
                int fd;
                ~Descriptor()
                {
                    if (fd >= 0)
                        ::close(fd);
                }
        };
        bool privateOwner(const struct stat &info) { return info.st_uid == ::geteuid() && !(info.st_mode & 0077); }
        bool plainName(std::string_view name)
        {
            return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos &&
                   name.find('\0') == std::string_view::npos;
        }
        bool readAll(int fd, std::uint8_t *out, std::size_t bytes)
        {
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
        bool writeAll(int fd, const std::uint8_t *in, std::size_t bytes)
        {
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

    PrivateDirectory::PrivateDirectory(const std::string &path)
    {
        if (path.empty() || path.find('\0') != std::string::npos)
            return;
        for (auto slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1))
            ::mkdir(path.substr(0, slash).c_str(), 0700);
        if (::mkdir(path.c_str(), 0700) && errno != EEXIST)
            return;
        Descriptor dir{::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        struct stat info{};
        if (dir.fd < 0 || ::fstat(dir.fd, &info) || !S_ISDIR(info.st_mode) || !privateOwner(info))
            return;
        m_fd = dir.fd;
        dir.fd = -1;
    }

    PrivateDirectory::~PrivateDirectory()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    PrivateDirectory::PrivateDirectory(PrivateDirectory &&other) noexcept : m_fd(other.m_fd) { other.m_fd = -1; }

    PrivateDirectory &PrivateDirectory::operator=(PrivateDirectory &&other) noexcept
    {
        if (this != &other) {
            if (m_fd >= 0)
                ::close(m_fd);
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }

    std::optional<std::vector<std::uint8_t>> PrivateDirectory::read(std::string_view name, std::size_t maxBytes) const
    {
        if (m_fd < 0 || !plainName(name))
            return std::nullopt;
        Descriptor file{::openat(m_fd, std::string(name).c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        struct stat info{};
        if (file.fd < 0 || ::fstat(file.fd, &info) || !S_ISREG(info.st_mode) || !privateOwner(info) || info.st_nlink != 1 ||
            std::uint64_t(info.st_size) > maxBytes)
            return std::nullopt;
        std::vector<std::uint8_t> bytes(std::size_t(info.st_size));
        if (!readAll(file.fd, bytes.data(), bytes.size()))
            return std::nullopt;
        return bytes;
    }

    bool PrivateDirectory::write(std::string_view name, std::span<const std::uint8_t> bytes) const
    {
        if (m_fd < 0 || !plainName(name))
            return false;
        static std::atomic<std::uint64_t> sequence{};
        const auto temporary = ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(sequence++);
        Descriptor file{::openat(m_fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (file.fd < 0)
            return false;
        const bool written = writeAll(file.fd, bytes.data(), bytes.size());
        if (!written || ::renameat(m_fd, temporary.c_str(), m_fd, std::string(name).c_str())) {
            ::unlinkat(m_fd, temporary.c_str(), 0);
            return false;
        }
        return true;
    }

    std::string PrivateDirectory::cacheRoot()
    {
        if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache && *cache == '/')
            return std::string(cache) + "/wemu";
        if (const char *home = std::getenv("HOME"); home && *home == '/')
            return std::string(home) + "/.cache/wemu";
        return {};
    }
} // namespace Utils
