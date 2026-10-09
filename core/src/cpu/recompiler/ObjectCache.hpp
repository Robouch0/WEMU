#pragma once

#include "Block.hpp"
#include <string>

namespace Core::Ppc {
    // Executable artifacts: only use a private, trusted local directory.
    // Guest dumps and shared/downloaded cache directories are never inputs.
    class ObjectCache {
        public:
            ObjectCache(const std::string &directory, std::string identity);
            ~ObjectCache();
            ObjectCache(const ObjectCache &) = delete;
            ObjectCache &operator=(const ObjectCache &) = delete;
            [[nodiscard]] bool available() const { return m_directory >= 0; }
            std::optional<std::vector<std::uint8_t>> load(const Block &block) const;
            bool store(const Block &block, std::span<const std::uint8_t> object) const;
        private:
            std::vector<std::uint8_t> key(const Block &block) const;
            int m_directory{-1};
            std::string m_identity;
    };
}
