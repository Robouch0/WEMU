#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#include "gfx/PipelineCacheStore.hpp"
#include "utils/PrivateDirectory.hpp"

namespace {
    struct TemporaryDirectory {
            std::filesystem::path path;
            TemporaryDirectory()
            {
                char name[] = "/tmp/wemu-pipeline-test-XXXXXX";
                const auto *result = ::mkdtemp(name);
                if (!result)
                    throw std::runtime_error("Cannot create temporary directory");
                path = result;
            }
            ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    };

    Core::Gfx::PipelineCacheIdentity identity()
    {
        Core::Gfx::PipelineCacheIdentity value{0x10DE, 0x2820, 0x91234567, {}};
        for (std::size_t i = 0; i < value.cacheUuid.size(); ++i)
            value.cacheUuid[i] = std::uint8_t(i * 7 + 1);
        return value;
    }

    // VkPipelineCacheHeaderVersionOne followed by opaque driver data.
    std::vector<std::uint8_t> driverData(const Core::Gfx::PipelineCacheIdentity &device, std::size_t payload = 64)
    {
        std::vector<std::uint8_t> data(32 + payload);
        const std::uint32_t words[]{32, 1, device.vendorId, device.deviceId};
        std::memcpy(data.data(), words, sizeof(words));
        std::copy(device.cacheUuid.begin(), device.cacheUuid.end(), data.begin() + 16);
        for (std::size_t i = 32; i < data.size(); ++i)
            data[i] = std::uint8_t(i * 13);
        return data;
    }
} // namespace

TEST(PrivateDirectoryTest, WritesAtomicallyAndRefusesForeignOrSharedFiles)
{
    TemporaryDirectory root;
    const auto path = (root.path / "nested" / "cache").string();
    Utils::PrivateDirectory directory(path);
    ASSERT_TRUE(directory.valid());
    struct stat info{};
    ASSERT_EQ(::stat(path.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0700u);

    const std::vector<std::uint8_t> bytes{1, 2, 3, 4};
    ASSERT_TRUE(directory.write("entry.bin", bytes));
    EXPECT_EQ(directory.read("entry.bin", 16), bytes);
    EXPECT_FALSE(directory.read("entry.bin", 3));
    EXPECT_FALSE(directory.write("../escape.bin", bytes));
    EXPECT_FALSE(directory.read("..", 16));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator{}), 1);

    ASSERT_EQ(::chmod((std::filesystem::path(path) / "entry.bin").c_str(), 0644), 0);
    EXPECT_FALSE(directory.read("entry.bin", 16));
    ASSERT_EQ(::link((std::filesystem::path(path) / "entry.bin").c_str(), (root.path / "hardlink").c_str()), 0);
    ASSERT_EQ(::chmod((std::filesystem::path(path) / "entry.bin").c_str(), 0600), 0);
    EXPECT_FALSE(directory.read("entry.bin", 16));

    ASSERT_EQ(::chmod(path.c_str(), 0755), 0);
    EXPECT_FALSE(Utils::PrivateDirectory(path).valid());
    std::filesystem::create_directory_symlink(path, root.path / "link");
    EXPECT_FALSE(Utils::PrivateDirectory((root.path / "link").string()).valid());
    EXPECT_FALSE(Utils::PrivateDirectory("").valid());
}

TEST(PipelineCacheStoreTest, RoundTripsDriverDataForTheSameDeviceAndDriver)
{
    using Core::Gfx::PipelineCacheStore;
    TemporaryDirectory root;
    const auto device = identity();
    const auto data = driverData(device);
    PipelineCacheStore store(root.path.string(), device);
    ASSERT_TRUE(store.enabled());
    EXPECT_TRUE(store.load().empty());
    ASSERT_TRUE(store.store(data));
    EXPECT_EQ(PipelineCacheStore(root.path.string(), device).load(), data);
    EXPECT_EQ(store.filename(), "10de-2820-91234567-01080f161d242b323940474e555c636a.bin");

    auto newerDriver = device;
    ++newerDriver.driverVersion;
    PipelineCacheStore updated(root.path.string(), newerDriver);
    EXPECT_NE(updated.filename(), store.filename());
    EXPECT_TRUE(updated.load().empty());
}

TEST(PipelineCacheStoreTest, CorruptTruncatedOrForeignDataIsNeverReturned)
{
    using Core::Gfx::PipelineCacheStore;
    TemporaryDirectory root;
    const auto device = identity();
    PipelineCacheStore store(root.path.string(), device);
    auto foreign = device;
    foreign.cacheUuid[3] ^= 0xFF;
    EXPECT_FALSE(store.store(driverData(foreign)));
    EXPECT_FALSE(store.store({}));
    EXPECT_FALSE(PipelineCacheStore::matchesDevice(std::vector<std::uint8_t>(31), device));

    ASSERT_TRUE(store.store(driverData(device)));
    const auto file = root.path / store.filename();
    const auto original = *Utils::PrivateDirectory(root.path.string()).read(store.filename(), 1 << 20);
    const auto rewrite = [&](const std::vector<std::uint8_t> &bytes) {
        std::filesystem::remove(file);
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        ASSERT_EQ(::chmod(file.c_str(), 0600), 0);
    };

    auto flipped = original;
    flipped.back() ^= 1;
    rewrite(flipped);
    EXPECT_TRUE(store.load().empty());

    rewrite({original.begin(), original.end() - 1});
    EXPECT_TRUE(store.load().empty());

    auto badMagic = original;
    badMagic[0] = 'X';
    rewrite(badMagic);
    EXPECT_TRUE(store.load().empty());

    rewrite(original);
    EXPECT_FALSE(store.load().empty());
    EXPECT_TRUE(PipelineCacheStore(root.path.string(), foreign).load().empty());
    EXPECT_FALSE(PipelineCacheStore("", device).enabled());
}
