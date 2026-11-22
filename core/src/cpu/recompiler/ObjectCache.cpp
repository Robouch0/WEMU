#include "ObjectCache.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/SHA256.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Core::Ppc {
    namespace {
        constexpr std::size_t maxObjectBytes = std::size_t{1024} * 1024;
        constexpr std::uint8_t magic[]{'W', 'E', 'M', 'U', 'O', 'B', 'J', 1};
        constexpr std::size_t headerSize = 8 + 32 + 32;
        struct File {
                int fd;
                ~File()
                {
                    if (fd >= 0)
                        ::close(fd);
                }
        };
        auto digest(std::span<const std::uint8_t> bytes) { return llvm::SHA256::hash(llvm::ArrayRef<std::uint8_t>(bytes.data(), bytes.size())); }
        std::string filename(std::span<const std::uint8_t> bytes)
        {
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            for (auto b: bytes) {
                result += hex[b >> 4];
                result += hex[b & 15];
            }
            return result + ".obj";
        }
        bool privateOwner(const struct stat &info) { return info.st_uid == ::geteuid() && !(info.st_mode & 0077); }
    } // namespace

    ObjectCache::ObjectCache(const std::string &directory, std::string identity) : m_identity(std::move(identity))
    {
        if (directory.empty())
            return;
        if (::mkdir(directory.c_str(), 0700) && errno != EEXIST)
            return;
        File dir{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        struct stat info{};
        if (dir.fd < 0 || ::fstat(dir.fd, &info) || !S_ISDIR(info.st_mode) || !privateOwner(info))
            return;
        m_directory = dir.fd;
        dir.fd = -1;
    }

    ObjectCache::~ObjectCache()
    {
        if (m_directory >= 0)
            ::close(m_directory);
    }

    std::vector<std::uint8_t> ObjectCache::key(const Block &block) const
    {
        std::vector<std::uint8_t> bytes(m_identity.begin(), m_identity.end());
        bytes.push_back(0);
        const auto append = [&](std::uint32_t value) {
            for (unsigned shift: {24u, 16u, 8u, 0u})
                bytes.push_back(value >> shift);
        };
        append(block.pc());
        append(block.words().size());
        for (auto word: block.words())
            append(word);
        const auto hash = digest(bytes);
        return {hash.begin(), hash.end()};
    }

    std::optional<std::vector<std::uint8_t>> ObjectCache::load(const Block &block) const
    {
        if (!available())
            return std::nullopt;
        const auto expected = key(block);
        File file{::openat(m_directory, filename(expected).c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        struct stat info{};
        if (file.fd < 0 || ::fstat(file.fd, &info) || !S_ISREG(info.st_mode) || !privateOwner(info) || info.st_nlink != 1 ||
            info.st_size <= headerSize || info.st_size > headerSize + maxObjectBytes)
            return std::nullopt;
        std::vector<std::uint8_t> bytes(info.st_size);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::read(file.fd, bytes.data() + offset, bytes.size() - offset);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return std::nullopt;
            offset += n;
        }
        if (!std::equal(std::begin(magic), std::end(magic), bytes.begin()) || !std::equal(expected.begin(), expected.end(), bytes.begin() + 8))
            return std::nullopt;
        const auto payload = std::span(bytes).subspan(headerSize);
        const auto checksum = digest(payload);
        if (!std::equal(checksum.begin(), checksum.end(), bytes.begin() + 40))
            return std::nullopt;
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }

    bool ObjectCache::store(const Block &block, std::span<const std::uint8_t> object) const
    {
        if (!available() || object.empty() || object.size() > maxObjectBytes)
            return false;
        const auto expected = key(block);
        const auto checksum = digest(object);
        std::vector<std::uint8_t> bytes(std::begin(magic), std::end(magic));
        bytes.insert(bytes.end(), expected.begin(), expected.end());
        bytes.insert(bytes.end(), checksum.begin(), checksum.end());
        bytes.insert(bytes.end(), object.begin(), object.end());
        static std::atomic<std::uint64_t> sequence{};
        const auto temporary = ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(sequence++);
        File file{::openat(m_directory, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (file.fd < 0)
            return false;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::write(file.fd, bytes.data() + offset, bytes.size() - offset);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            offset += n;
        }
        const bool success = offset == bytes.size() && ::renameat(m_directory, temporary.c_str(), m_directory, filename(expected).c_str()) == 0;
        if (!success)
            ::unlinkat(m_directory, temporary.c_str(), 0);
        return success;
    }
} // namespace Core::Ppc
