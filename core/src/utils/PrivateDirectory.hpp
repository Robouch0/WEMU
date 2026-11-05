#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Utils {
    // A cache directory only the current user can reach. Files are read only when regular, private and
    // singly linked, and written through a private temporary file renamed into place, so a crash or a
    // concurrent session never leaves a partial file behind.
    class PrivateDirectory {
        public:
            PrivateDirectory() = default;
            // Creates missing parents. Stays invalid for an empty path, a symlink, or a directory that is
            // foreign-owned or accessible to other users.
            explicit PrivateDirectory(const std::string &path);
            ~PrivateDirectory();
            PrivateDirectory(PrivateDirectory &&other) noexcept;
            PrivateDirectory &operator=(PrivateDirectory &&other) noexcept;
            PrivateDirectory(const PrivateDirectory &) = delete;
            PrivateDirectory &operator=(const PrivateDirectory &) = delete;

            bool valid() const { return m_fd >= 0; }
            std::optional<std::vector<std::uint8_t>> read(std::string_view name, std::size_t maxBytes) const;
            bool write(std::string_view name, std::span<const std::uint8_t> bytes) const;

            // $XDG_CACHE_HOME/wemu, else ~/.cache/wemu; empty when neither is an absolute path.
            static std::string cacheRoot();

        private:
            int m_fd{-1};
    };
} // namespace Utils
