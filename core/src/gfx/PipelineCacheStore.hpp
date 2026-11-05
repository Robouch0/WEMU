#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "utils/PrivateDirectory.hpp"

namespace Core::Gfx {
    struct PipelineCacheIdentity {
            std::uint32_t vendorId{}, deviceId{}, driverVersion{};
            std::array<std::uint8_t, 16> cacheUuid{};
    };

    // Driver pipeline-cache data kept between sessions, one file per device, driver version and cache
    // UUID. Data reaches the driver only when its checksum and Vulkan header match this device, so a
    // corrupt, truncated or foreign file starts an empty cache instead.
    class PipelineCacheStore {
        public:
            static constexpr std::size_t maxBytes = std::size_t{256} * 1024 * 1024;

            // WEMU_PIPELINE_CACHE_DIR (empty disables), else <cache root>/pipelines.
            static std::string defaultDirectory();
            PipelineCacheStore(const std::string &directory, const PipelineCacheIdentity &identity);

            bool enabled() const { return m_directory.valid(); }
            const std::string &filename() const { return m_filename; }
            std::vector<std::uint8_t> load() const;
            bool store(std::span<const std::uint8_t> data) const;
            // VkPipelineCacheHeaderVersionOne check: header version one, this vendor, device and UUID.
            static bool matchesDevice(std::span<const std::uint8_t> data, const PipelineCacheIdentity &identity);

        private:
            Utils::PrivateDirectory m_directory;
            PipelineCacheIdentity m_identity;
            std::string m_filename;
    };
} // namespace Core::Gfx
