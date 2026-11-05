#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "hle/Coreinit.hpp"
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

        void TearDown() override
        {
            SetFsSdRoot("");
            std::filesystem::remove_all(root);
        }

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

TEST_F(FsTest, OptionalSdMountServesFilesAndUnmountRemovesAccess)
{
    constexpr unsigned SOURCE = 0x02101000, TARGET = 0x02101400;
    constexpr unsigned PATH = 0x02101500, MODE = 0x02101600, OUT = 0x02101700;
    const auto source = [&] {
        cpu->m_gpr[5] = 0;
        cpu->m_gpr[6] = SOURCE;
        call("FSGetMountSource");
    };
    SetFsSdRoot("");
    source();
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), -6);
    SetFsSdRoot(root.string());
    source();
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_gpr[5] = SOURCE;
    cpu->m_gpr[6] = TARGET;
    cpu->m_gpr[7] = 4; // too small: must not mount or truncate the path
    call("FSMount");
    EXPECT_LT(static_cast<std::int32_t>(cpu->m_gpr[3]), 0);
    cpu->m_gpr[5] = SOURCE;
    cpu->m_gpr[6] = TARGET;
    cpu->m_gpr[7] = 128;
    call("FSMount");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_STREQ(reinterpret_cast<char *>(cpu->m_memory.hostPtr(TARGET)), "/vol/external01");
    putStr(MODE, "rb");
    const auto open = [&](const std::string &path) {
        putStr(PATH, path);
        cpu->m_gpr[3] = 0;
        cpu->m_gpr[5] = PATH;
        cpu->m_gpr[6] = MODE;
        cpu->m_gpr[7] = OUT;
        call("FSOpenFile");
    };
    open("/vol/external01/ui/hello.bin");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_gpr[5] = cpu->m_memory.read<unsigned>(OUT);
    call("FSCloseFile");
    open("/vol/external01/../../ui/hello.bin");
    EXPECT_LT(static_cast<std::int32_t>(cpu->m_gpr[3]), 0);
    cpu->m_gpr[5] = TARGET;
    call("FSUnmount");
    open("/vol/external01/ui/hello.bin");
    EXPECT_LT(static_cast<std::int32_t>(cpu->m_gpr[3]), 0);
}

TEST_F(FsTest, OpenFileExWritesHandleToEighthArgumentAndMakeDirCreatesDirectory)
{
    constexpr unsigned PATH = 0x02100000, MODE = 0x02100100, OUT = 0x02100200, BUFFER = 0x02100300;
    putStr(PATH, "/vol/content/config");
    cpu->m_gpr[5] = PATH;
    call("FSMakeDir");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_TRUE(std::filesystem::is_directory(root / "config"));
    putStr(PATH, "/vol/content/ui/hello.bin");
    putStr(MODE, "r");
    cpu->m_gpr[5] = PATH;
    cpu->m_gpr[6] = MODE;
    cpu->m_gpr[7] = 0660; // permission bits, not an output pointer
    cpu->m_gpr[8] = 0;
    cpu->m_gpr[9] = 0;
    cpu->m_gpr[10] = OUT;
    call("FSOpenFileEx");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    const auto handle = cpu->m_memory.read<unsigned>(OUT);
    ASSERT_NE(handle, 0u);
    cpu->m_gpr[5] = BUFFER;
    cpu->m_gpr[6] = 1;
    cpu->m_gpr[7] = 8;
    cpu->m_gpr[8] = handle;
    call("FSReadFile");
    EXPECT_EQ(cpu->m_gpr[3], 8u);
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(BUFFER), 'M');
    cpu->m_gpr[5] = handle;
    call("FSCloseFile");
}

TEST_F(FsTest, SharedSavePathsSupportDatabaseCreationAndKeepTitlesSeparate)
{
    // Keep persistent test saves inside the fixture's disposable directory.
    SetFsContentRoot((root / "content").string());
    constexpr unsigned FILE = 0x02100000, PATH = 0x02100100, MODE = 0x02100300;
    constexpr unsigned OUT = 0x02100400, BUFFER = 0x02100500;
    putStr(FILE, "db/database.bin");
    const auto sharedPath = [&](unsigned titleLow) {
        cpu->m_gpr[3] = 0x0005001B;
        cpu->m_gpr[4] = titleLow;
        cpu->m_gpr[5] = FILE;
        cpu->m_gpr[6] = PATH;
        cpu->m_gpr[7] = 0x200;
        call("SAVEGetSharedSaveDataPath");
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        return std::string(reinterpret_cast<char *>(cpu->m_memory.hostPtr(PATH)));
    };
    const auto open = [&](const char *mode) {
        putStr(MODE, mode);
        cpu->m_gpr[3] = 0;
        cpu->m_gpr[5] = PATH;
        cpu->m_gpr[6] = MODE;
        cpu->m_gpr[7] = OUT;
        call("FSOpenFile");
    };
    const auto first = sharedPath(0x10056000);
    open("r");
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), -6);
    open("w");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    const auto handle = cpu->m_memory.read<unsigned>(OUT);
    cpu->m_memory.write<unsigned>(BUFFER, 0x12345678);
    cpu->m_gpr[5] = BUFFER;
    cpu->m_gpr[6] = 1;
    cpu->m_gpr[7] = 4;
    cpu->m_gpr[8] = handle;
    call("FSWriteFile");
    EXPECT_EQ(cpu->m_gpr[3], 4u);
    cpu->m_gpr[5] = handle;
    call("FSCloseFile");

    EXPECT_NE(sharedPath(0x10057000), first);
    open("r");
    EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), -6);

    EXPECT_EQ(sharedPath(0x10056000), first);
    open("r");
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    const auto reopened = cpu->m_memory.read<unsigned>(OUT);
    cpu->m_memory.write<unsigned>(BUFFER, 0);
    cpu->m_gpr[5] = BUFFER;
    cpu->m_gpr[6] = 1;
    cpu->m_gpr[7] = 4;
    cpu->m_gpr[8] = reopened;
    call("FSReadFile");
    EXPECT_EQ(cpu->m_gpr[3], 4u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(BUFFER), 0x12345678u);
    cpu->m_gpr[5] = reopened;
    call("FSCloseFile");
}

TEST_F(FsTest, AsyncStatDeliversSuccessAndErrorsThroughCallbacksAndQueues)
{
    RegisterCoreinitFunctions();
    cpu->m_scheduler = Core::Scheduler{};
    cpu->m_scheduler.bootstrap(*cpu);
    const auto main = cpu->m_scheduler.mainHandle();
    constexpr unsigned CLIENT = 0x02101000, BLOCK = 0x02103004, DATA = 0x02104000;
    constexpr unsigned PATH = 0x02100000, STAT = 0x02100300, CALLBACK = 0x02000800;
    constexpr unsigned QUEUE = 0x02104100, STORAGE = 0x02104200, MESSAGE = 0x02104300;
    constexpr unsigned RESULT = ((BLOCK + 0x3F) & ~0x3Fu) + 0x96C;
    const auto submit = [&] {
        cpu->m_gpr[3] = CLIENT;
        cpu->m_gpr[4] = BLOCK;
        cpu->m_gpr[5] = PATH;
        cpu->m_gpr[6] = STAT;
        cpu->m_gpr[7] = ~0u;
        cpu->m_gpr[8] = DATA;
        call("FSGetStatAsync");
    };
    cpu->m_memory.write<unsigned>(DATA, CALLBACK);
    cpu->m_memory.write<unsigned>(DATA + 4, 0x12345678);
    cpu->m_memory.write<unsigned>(DATA + 8, 0);
    for (const bool exists: {true, false}) {
        putStr(PATH, exists ? "/vol/content/ui/hello.bin" : "/vol/content/missing.bin");
        const unsigned status = exists ? 0u : static_cast<unsigned>(-6);
        submit();
        ASSERT_EQ(cpu->m_gpr[3], 0u); // accepted even when the operation reports NOT_FOUND
        EXPECT_EQ(cpu->m_scheduler.currentHandle(), main); // callback is deferred
        if (exists)
            EXPECT_EQ(cpu->m_memory.read<unsigned>(STAT + 0x10), 8u);
        Core::Async::OnTick(*cpu);
        ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
        ASSERT_NE(cpu->m_scheduler.currentHandle(), main);
        EXPECT_EQ(cpu->m_pc, CALLBACK - Core::Memory::MemoryMap::ApplicationCode);
        EXPECT_EQ(cpu->m_gpr[3], CLIENT);
        EXPECT_EQ(cpu->m_gpr[4], BLOCK);
        EXPECT_EQ(cpu->m_gpr[5], status);
        EXPECT_EQ(cpu->m_gpr[6], 0x12345678u);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(RESULT + 0x24), status);
        Core::Async::OnSentinelReturn(*cpu);
        ASSERT_EQ(cpu->m_scheduler.currentHandle(), main);
    }
    cpu->m_gpr[3] = QUEUE;
    cpu->m_gpr[4] = STORAGE;
    cpu->m_gpr[5] = 1;
    call("OSInitMessageQueue");
    cpu->m_memory.write<unsigned>(DATA, 0);
    cpu->m_memory.write<unsigned>(DATA + 8, QUEUE);
    putStr(PATH, "/vol/content/ui/hello.bin");
    submit();
    ASSERT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), main);
    submit(); // a full queue must report failure rather than silently lose completion
    EXPECT_EQ(static_cast<int>(cpu->m_gpr[3]), -0x400);
    cpu->m_gpr[3] = QUEUE;
    cpu->m_gpr[4] = MESSAGE;
    cpu->m_gpr[5] = 0;
    call("OSReceiveMessage");
    ASSERT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(MESSAGE + 0x0C), 8u);
    cpu->m_gpr[3] = MESSAGE;
    call("FSGetAsyncResult");
    ASSERT_EQ(cpu->m_gpr[3], RESULT);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(RESULT + 0x1C), CLIENT);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(RESULT + 0x20), BLOCK);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(RESULT + 0x24), 0u);
    cpu->m_memory.write<unsigned>(MESSAGE + 0x0C, 9);
    cpu->m_gpr[3] = MESSAGE;
    call("FSGetAsyncResult");
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_memory.write<unsigned>(DATA + 8, 0);
    submit();
    EXPECT_EQ(static_cast<int>(cpu->m_gpr[3]), -0x400);
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
