#ifdef WEMU_HAS_LLVM
    #include <array>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <gtest/gtest.h>
    #include <sys/stat.h>
    #include <unistd.h>

    #include "cpu/recompiler/Dispatcher.hpp"

namespace {
    struct TemporaryCache {
            std::filesystem::path path;
            TemporaryCache()
            {
                char name[] = "/tmp/wemu-object-test-XXXXXX";
                const auto *result = ::mkdtemp(name);
                if (!result)
                    throw std::runtime_error("Cannot create temporary cache");
                path = result;
            }
            ~TemporaryCache() { std::filesystem::remove_all(path); }
    };
    const std::vector<std::uint32_t> words{0x38600001, 0x38630002};
} // namespace

TEST(PpcObjectCacheTest, ReloadsNativeCodeAndSeparatesCodeAndCompilerIdentity)
{
    using namespace Core::Ppc;
    TemporaryCache directory;
    {
        Dispatcher cold(16, 1, directory.path.string());
        ASSERT_TRUE(cold.cacheEnabled());
        std::array<std::uint32_t, 32> regs{};
        EXPECT_EQ(cold.execute(0x02000000, words, regs, 2), 2u);
        EXPECT_EQ(regs[3], 3u);
        EXPECT_EQ(cold.stats().compiled, 1u);
        EXPECT_EQ(cold.stats().cacheWrites, 1u);
    }
    Dispatcher warm(16, 1, directory.path.string());
    std::array<std::uint32_t, 32> regs{};
    EXPECT_EQ(warm.execute(0x02000000, words, regs, 1), 0u);
    EXPECT_EQ(warm.stats().cacheHits, 0u);
    EXPECT_EQ(warm.execute(0x02000000, words, regs, 2), 2u);
    EXPECT_EQ(regs[3], 3u);
    EXPECT_EQ(warm.stats().compiled, 0u);
    EXPECT_EQ(warm.stats().cacheHits, 1u);
    const auto block = Block::decode(0x02000000, words);
    ASSERT_TRUE(block);
    ObjectCache changedCompiler(directory.path.string(), nativeObjectIdentity() + "changed");
    EXPECT_FALSE(changedCompiler.load(*block));
    ObjectCache cache(directory.path.string(), nativeObjectIdentity());
    EXPECT_TRUE(cache.load(*block));
    EXPECT_FALSE(cache.load(*Block::decode(0x02000004, words)));
    auto modified = words;
    modified[1] = 0x38630004;
    EXPECT_EQ(warm.execute(0x02000000, modified, regs, 2), 2u);
    EXPECT_EQ(regs[3], 5u);
    EXPECT_EQ(warm.stats().invalidations, 1u);
    EXPECT_EQ(warm.stats().compiled, 1u);
}

TEST(PpcObjectCacheTest, RejectsCorruptionPermissionsAndSymlinks)
{
    using namespace Core::Ppc;
    TemporaryCache directory;
    const auto block = Block::decode(0x02000000, words);
    ASSERT_TRUE(block);
    const auto object = emitObject(*block);
    ObjectCache cache(directory.path.string(), nativeObjectIdentity());
    ASSERT_TRUE(cache.store(*block, object));
    const auto file = std::filesystem::directory_iterator(directory.path)->path();
    {
        std::fstream corrupt(file, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekp(72);
        corrupt.put(0);
    }
    EXPECT_FALSE(cache.load(*block));
    ASSERT_TRUE(cache.store(*block, object));
    ASSERT_EQ(::chmod(file.c_str(), 0644), 0);
    EXPECT_FALSE(cache.load(*block));
    std::filesystem::remove(file);
    std::filesystem::create_symlink("/dev/zero", file);
    EXPECT_FALSE(cache.load(*block));
    ASSERT_TRUE(cache.store(*block, object)); // atomic replace, never follows the link
    EXPECT_TRUE(cache.load(*block));
    std::filesystem::resize_file(file, 12);
    EXPECT_FALSE(cache.load(*block));
    ASSERT_TRUE(cache.store(*block, object));
    std::filesystem::resize_file(file, 2 * 1024 * 1024);
    EXPECT_FALSE(cache.load(*block));
    ASSERT_EQ(::chmod(directory.path.c_str(), 0755), 0);
    ObjectCache insecure(directory.path.string(), nativeObjectIdentity());
    EXPECT_FALSE(insecure.available());
    const auto link = directory.path / "link";
    std::filesystem::create_directory_symlink(directory.path, link);
    ObjectCache symlink(link.string(), nativeObjectIdentity());
    EXPECT_FALSE(symlink.available());
}
#endif
