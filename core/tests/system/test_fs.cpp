#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/Fs.hpp"

// Host-backed Cafe filesystem HLE: open/stat/read/close against a temp content root.
class FsTest : public InstructionTest {
    protected:
        std::filesystem::path root;

        void SetUp() override
        {
            InstructionTest::SetUp();
            root = std::filesystem::temp_directory_path() / "wemu_fs_test";
            std::filesystem::create_directories(root / "ui");
            std::ofstream(root / "ui" / "hello.bin", std::ios::binary) << "MK8DATA!"; // 8 bytes
            SetFsContentRoot(root.string());
            RegisterFsFunctions();
        }

        void TearDown() override { std::filesystem::remove_all(root); }

        // Writes a NUL-terminated string into guest memory.
        void putStr(std::uint32_t addr, const std::string &s)
        {
            for (std::size_t i = 0; i < s.size(); i++)
                cpu->m_memory.write<std::uint8_t>(addr + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(s[i]));
            cpu->m_memory.write<std::uint8_t>(addr + static_cast<std::uint32_t>(s.size()), 0);
        }

        static void call(const char *name) { Core::syscallHandler.get(name)(*cpu); }
};

TEST_F(FsTest, OpenStatReadClose)
{
    constexpr std::uint32_t PATH = 0x02100000, MODE = 0x02100100, OUT = 0x02100200;
    constexpr std::uint32_t STAT = 0x02100300, BUF = 0x02100400;

    putStr(PATH, "/vol/content/ui/hello.bin");
    putStr(MODE, "rb");

    // FSOpenFile(client, cmd, path, mode, *outHandle, errMask)
    cpu->m_gpr[5] = PATH;
    cpu->m_gpr[6] = MODE;
    cpu->m_gpr[7] = OUT;
    call("FSOpenFile");
    ASSERT_EQ(cpu->m_gpr[3], 0u); // FS_STATUS_OK
    const std::uint32_t handle = cpu->m_memory.read<std::uint32_t>(OUT);
    ASSERT_NE(handle, 0u);

    // FSGetStatFile(client, cmd, handle, *stat, errMask) -> size at stat+0x10
    cpu->m_gpr[5] = handle;
    cpu->m_gpr[6] = STAT;
    call("FSGetStatFile");
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(STAT + 0x10), 8u);

    // FSReadFile(client, cmd, buffer, size, count, handle, unk, errMask)
    cpu->m_gpr[5] = BUF;
    cpu->m_gpr[6] = 1; // size
    cpu->m_gpr[7] = 8; // count
    cpu->m_gpr[8] = handle;
    call("FSReadFile");
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), 8); // 8 one-byte blocks read
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(BUF + 0), 'M');
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(BUF + 7), '!');

    cpu->m_gpr[5] = handle;
    call("FSCloseFile");
    EXPECT_EQ(cpu->m_gpr[3], 0u);
}

TEST_F(FsTest, OpenMissingFileFails)
{
    constexpr std::uint32_t PATH = 0x02100000, MODE = 0x02100100, OUT = 0x02100200;
    putStr(PATH, "/vol/content/nope/missing.bin");
    putStr(MODE, "rb");
    cpu->m_gpr[5] = PATH;
    cpu->m_gpr[6] = MODE;
    cpu->m_gpr[7] = OUT;
    call("FSOpenFile");
    EXPECT_LT(static_cast<std::int32_t>(cpu->m_gpr[3]), 0); // negative FSStatus
}

TEST_F(FsTest, VolumeIsReadyWhenContentIsMounted)
{
    call("FSGetVolumeState");
    EXPECT_EQ(cpu->m_gpr[3], 1u); // FS_VOLUME_STATE_READY, not FS_STATUS_OK

    SetFsContentRoot((root / "unmounted").string());
    call("FSGetVolumeState");
    EXPECT_EQ(cpu->m_gpr[3], 2u); // FS_VOLUME_STATE_NO_MEDIA
}

TEST_F(FsTest, WorkingDirectoriesArePerClientAndStartAtRoot)
{
    constexpr std::uint32_t CLIENT = 0x02101000, OTHER = 0x02103000;
    constexpr std::uint32_t PATH = 0x02100000, BUFFER = 0x02100400;
    for (auto client: {CLIENT, OTHER}) {
        cpu->m_gpr[3] = client;
        call("FSAddClient");
    }
    auto getCwd = [&](std::uint32_t client) {
        cpu->m_gpr[3] = client;
        cpu->m_gpr[5] = BUFFER;
        cpu->m_gpr[6] = 0x27F;
        call("FSGetCwd");
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        return std::string(reinterpret_cast<char *>(cpu->m_memory.hostPtr(BUFFER)));
    };
    EXPECT_EQ(getCwd(CLIENT), "/");
    putStr(PATH, "/vol/content/ui");
    cpu->m_gpr[3] = CLIENT;
    cpu->m_gpr[5] = PATH;
    call("FSChangeDir");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(getCwd(CLIENT), "/vol/content/ui/");
    EXPECT_EQ(getCwd(OTHER), "/");

    putStr(PATH, "missing");
    cpu->m_gpr[3] = CLIENT;
    cpu->m_gpr[5] = PATH;
    call("FSChangeDir");
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), -6);
    EXPECT_EQ(getCwd(CLIENT), "/vol/content/ui/");

    cpu->m_gpr[3] = CLIENT;
    call("FSDelClient");
    cpu->m_gpr[3] = CLIENT;
    call("FSAddClient");
    EXPECT_EQ(getCwd(CLIENT), "/");
}

TEST_F(FsTest, RelativePathsRespectCwdAndDoNotRepairDuplicatedMounts)
{
    constexpr std::uint32_t CLIENT = 0x02101000, PATH = 0x02100000;
    constexpr std::uint32_t MODE = 0x02100100, OUT = 0x02100200, STAT = 0x02100300;
    cpu->m_gpr[3] = CLIENT;
    call("FSAddClient");
    putStr(PATH, "/vol/content/ui");
    cpu->m_gpr[3] = CLIENT;
    cpu->m_gpr[5] = PATH;
    call("FSChangeDir");
    ASSERT_EQ(cpu->m_gpr[3], 0u);

    for (const auto *path: {"hello.bin", "../ui/./hello.bin", "/vol/content//ui/hello.bin"}) {
        putStr(PATH, path);
        putStr(MODE, "rb");
        cpu->m_gpr[3] = CLIENT;
        cpu->m_gpr[5] = PATH;
        cpu->m_gpr[6] = MODE;
        cpu->m_gpr[7] = OUT;
        call("FSOpenFile");
        ASSERT_EQ(cpu->m_gpr[3], 0u) << path;
        cpu->m_gpr[5] = cpu->m_memory.read<std::uint32_t>(OUT);
        call("FSCloseFile");

        cpu->m_gpr[3] = CLIENT;
        cpu->m_gpr[5] = PATH;
        cpu->m_gpr[6] = STAT;
        call("FSGetStat");
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(STAT + 0x10), 8u);
    }

    for (const auto &path:
         {std::string("/vol/content/vol/content/ui/hello.bin"), std::string("/vol/content-other/ui/hello.bin"), (root / "ui/hello.bin").string()}) {
        putStr(PATH, path);
        cpu->m_gpr[3] = CLIENT;
        cpu->m_gpr[5] = PATH;
        cpu->m_gpr[6] = MODE;
        cpu->m_gpr[7] = OUT;
        call("FSOpenFile");
        EXPECT_LT(static_cast<std::int32_t>(cpu->m_gpr[3]), 0) << path;
    }
}

TEST_F(FsTest, GetCwdRejectsShortBufferWithoutWriting)
{
    constexpr std::uint32_t BUFFER = 0x02100400;
    putStr(BUFFER, "untouched");
    cpu->m_gpr[5] = BUFFER;
    cpu->m_gpr[6] = 1;
    call("FSGetCwd");
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), -0x400);
    EXPECT_STREQ(reinterpret_cast<char *>(cpu->m_memory.hostPtr(BUFFER)), "untouched");
}

TEST_F(FsTest, ReadFileWithPos)
{
    constexpr std::uint32_t PATH = 0x02100000, MODE = 0x02100100, OUT = 0x02100200, BUF = 0x02100400;
    putStr(PATH, "/vol/content/ui/hello.bin");
    putStr(MODE, "rb");
    cpu->m_gpr[5] = PATH;
    cpu->m_gpr[6] = MODE;
    cpu->m_gpr[7] = OUT;
    call("FSOpenFile");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    const std::uint32_t handle = cpu->m_memory.read<std::uint32_t>(OUT);

    // FSReadFileWithPos(client, cmd, buffer, size, count, pos, handle, unk) -> read "DATA" at offset 3
    cpu->m_gpr[5] = BUF;
    cpu->m_gpr[6] = 1;
    cpu->m_gpr[7] = 4;
    cpu->m_gpr[8] = 3; // pos
    cpu->m_gpr[9] = handle;
    call("FSReadFileWithPos");
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), 4);
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(BUF + 0), 'D');
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(BUF + 3), 'A');
}
