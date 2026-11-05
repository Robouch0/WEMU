#include "gfx/PipelineCacheStore.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>

namespace Core::Gfx {
    namespace {
        constexpr char kMagic[8]{'W', 'E', 'M', 'U', 'P', 'L', 'C', '1'};
        // magic, data bytes, FNV-1a of the data
        constexpr std::size_t kHeaderBytes = sizeof(kMagic) + 2 * sizeof(std::uint64_t);

        std::uint64_t fnv1a(std::span<const std::uint8_t> data)
        {
            std::uint64_t hash = 0xCBF29CE484222325ull;
            for (const auto byte: data)
                hash = (hash ^ byte) * 0x100000001B3ull;
            return hash;
        }
        std::uint32_t readWord(std::span<const std::uint8_t> data, std::size_t offset)
        {
            std::uint32_t value{};
            std::memcpy(&value, data.data() + offset, sizeof(value));
            return value;
        }
    } // namespace

    std::string PipelineCacheStore::defaultDirectory()
    {
        if (const char *configured = std::getenv("WEMU_PIPELINE_CACHE_DIR"))
            return configured;
        const auto root = Utils::PrivateDirectory::cacheRoot();
        return root.empty() ? root : root + "/pipelines";
    }

    PipelineCacheStore::PipelineCacheStore(const std::string &directory, const PipelineCacheIdentity &identity) :
        m_directory(directory), m_identity(identity)
    {
        m_filename = std::format("{:04x}-{:04x}-{:08x}-", identity.vendorId, identity.deviceId, identity.driverVersion);
        for (const auto byte: identity.cacheUuid)
            m_filename += std::format("{:02x}", byte);
        m_filename += ".bin";
    }

    bool PipelineCacheStore::matchesDevice(std::span<const std::uint8_t> data, const PipelineCacheIdentity &identity)
    {
        constexpr std::size_t headerVersionOne = 32;
        if (data.size() < headerVersionOne)
            return false;
        const auto headerSize = readWord(data, 0);
        return headerSize >= headerVersionOne && headerSize <= data.size() && readWord(data, 4) == 1 && readWord(data, 8) == identity.vendorId &&
               readWord(data, 12) == identity.deviceId && std::equal(identity.cacheUuid.begin(), identity.cacheUuid.end(), data.begin() + 16);
    }

    std::vector<std::uint8_t> PipelineCacheStore::load() const
    {
        const auto file = m_directory.read(m_filename, kHeaderBytes + maxBytes);
        if (!file || file->size() < kHeaderBytes || !std::equal(std::begin(kMagic), std::end(kMagic), file->begin()))
            return {};
        std::uint64_t bytes{}, hash{};
        std::memcpy(&bytes, file->data() + sizeof(kMagic), sizeof(bytes));
        std::memcpy(&hash, file->data() + sizeof(kMagic) + sizeof(bytes), sizeof(hash));
        const auto data = std::span(*file).subspan(kHeaderBytes);
        if (bytes != data.size() || hash != fnv1a(data) || !matchesDevice(data, m_identity))
            return {};
        return {data.begin(), data.end()};
    }

    bool PipelineCacheStore::store(std::span<const std::uint8_t> data) const
    {
        if (!enabled() || data.empty() || data.size() > maxBytes || !matchesDevice(data, m_identity))
            return false;
        std::vector<std::uint8_t> file(kHeaderBytes + data.size());
        const std::uint64_t bytes = data.size(), hash = fnv1a(data);
        std::memcpy(file.data(), kMagic, sizeof(kMagic));
        std::memcpy(file.data() + sizeof(kMagic), &bytes, sizeof(bytes));
        std::memcpy(file.data() + sizeof(kMagic) + sizeof(bytes), &hash, sizeof(hash));
        std::memcpy(file.data() + kHeaderBytes, data.data(), data.size());
        return m_directory.write(m_filename, file);
    }
} // namespace Core::Gfx
