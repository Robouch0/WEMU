// coreinit services beyond the core primitives: events, run-time linking,
// zlib compression, the shared system font and the console logs.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/Instruction.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

namespace {
    constexpr std::uint32_t App = Core::Memory::MemoryMap::ApplicationCode;
    constexpr std::uint32_t WorkerA = 0x2000, WorkerB = 0x3000;
    constexpr std::uint32_t ManualReset = 0, AutoReset = 1;
} // namespace

class CoreinitExtraTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hle_redirected = false;
            cpu->m_interruptsDisabled = false;
            cpu->m_running = true;
            RegisterCoreinitFunctions();
            RegisterCoreinitExtraFunctions();
        }

        static std::uint32_t call(Core::Interpreter &target, const char *name, std::initializer_list<std::uint32_t> args)
        {
            std::uint32_t reg = 3;
            for (const auto value: args)
                target.m_gpr[reg++] = value;
            target.m_hle_redirected = false;
            Core::syscallHandler.get(name)(target);
            return target.m_gpr[3];
        }

        static std::uint32_t call(const char *name, std::initializer_list<std::uint32_t> args = {}) { return call(*cpu, name, args); }

        void putString(std::uint32_t at, const std::string &text)
        {
            for (std::size_t i = 0; i <= text.size(); ++i)
                cpu->m_memory.write<std::uint8_t>(at + static_cast<std::uint32_t>(i), i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0);
        }

        void startWorkers()
        {
            cpu->m_scheduler.bootstrap(*cpu);
            cpu->m_scheduler.create(*cpu, WorkerA, App + 0x200, 0, 0, 0xC0000000);
            cpu->m_scheduler.create(*cpu, WorkerB, App + 0x300, 0, 0, 0xC0001000);
            cpu->m_scheduler.resume(WorkerA);
            cpu->m_scheduler.resume(WorkerB);
        }

        // OSWaitEventWithTimeout with a 1 s timeout: 1 when the event was already signaled.
        static std::uint32_t waitOneSecond(std::uint32_t event) { return call("OSWaitEventWithTimeout", {event, 0, 0, 1000000000u}); }
};

TEST_F(CoreinitExtraTest, ManualResetEventReleasesEveryWaiterAndStaysSignaledUntilReset)
{
    startWorkers();
    const auto *main = cpu->m_scheduler.current();
    constexpr std::uint32_t Event = 0x28300000;
    call("OSInitEvent", {Event, 0, ManualReset});

    call("OSWaitEvent", {Event});
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), WorkerA) << "main waits, a worker runs";
    call("OSWaitEvent", {Event});
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), WorkerB);
    const auto *workerA = cpu->m_scheduler.thread(WorkerA);
    ASSERT_NE(workerA, nullptr);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    EXPECT_EQ(workerA->state, Core::ThreadContext::State::Waiting);

    call("OSSignalEventAll", {Event});
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(workerA->state, Core::ThreadContext::State::Ready);

    // Still signaled: a later wait passes straight through.
    EXPECT_EQ(call("OSWaitEvent", {Event}), 0u);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), WorkerB);
    EXPECT_EQ(waitOneSecond(Event), 1u);
    EXPECT_EQ(waitOneSecond(Event), 1u) << "a manual-reset event is not consumed by waiting";

    call("OSResetEvent", {Event});
    call("OSWaitEvent", {Event});
    EXPECT_NE(cpu->m_scheduler.currentHandle(), WorkerB) << "after a reset the waiter blocks again";
}

TEST_F(CoreinitExtraTest, AutoResetEventIsConsumedByExactlyOneWait)
{
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr std::uint32_t Event = 0x28310000;
    call("OSInitEvent", {Event, 0, AutoReset});

    call("OSSignalEvent", {Event}); // no waiter: the signal is kept for the next wait
    EXPECT_EQ(waitOneSecond(Event), 1u);
    EXPECT_EQ(waitOneSecond(Event), 0u) << "the first wait consumed it; alone, the second times out";

    call("OSInitEvent", {Event, 1, AutoReset}); // created signaled
    EXPECT_EQ(call("OSWaitEvent", {Event}), 0u);
    EXPECT_EQ(waitOneSecond(Event), 0u);
}

TEST_F(CoreinitExtraTest, SignalAllOnAnAutoResetEventIsConsumedByTheThreadsItWakes)
{
    startWorkers();
    const auto *main = cpu->m_scheduler.current();
    constexpr std::uint32_t Event = 0x28320000;
    call("OSInitEvent", {Event, 0, AutoReset});
    call("OSWaitEvent", {Event});
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), WorkerA);

    call("OSSignalEventAll", {Event});
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready);
    // The woken waiter took the signal, so this wait must block rather than see it.
    auto *waiter = cpu->m_scheduler.current();
    call("OSWaitEventWithTimeout", {Event, 0, 0, 1000000u});
    EXPECT_EQ(waiter->state, Core::ThreadContext::State::Waiting);
    EXPECT_NE(cpu->m_scheduler.currentHandle(), waiter->osThreadPtr);
}

TEST_F(CoreinitExtraTest, ExportsFoundAtRunTimeAreCallableThroughBctrl)
{
    constexpr std::uint32_t Name = 0x28330000, Out = 0x28330100;
    putString(Name, "OSGetCoreCount");
    ASSERT_EQ(call("OSDynLoad_FindExport", {0xD11B0001u, 0, Name, Out}), 0u);
    const auto pointer = cpu->m_memory.read<std::uint32_t>(Out);
    ASSERT_GE(pointer, 0xC0000000u) << "exports resolve to a sentinel outside guest code";

    putString(Name, "OSGetCoreCount");
    call("OSDynLoad_FindExport", {0xD11B0001u, 0, Name, Out});
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Out), pointer) << "the same export always has the same address";
    putString(Name, "OSGetMainCoreId");
    call("OSDynLoad_FindExport", {0xD11B0001u, 0, Name, Out});
    EXPECT_NE(cpu->m_memory.read<std::uint32_t>(Out), pointer);

    // mtctr r12 ; bctrl -- how titles call through a function pointer.
    cpu->m_pc = 0x400;
    cpu->m_ctr = pointer;
    cpu->m_gpr[3] = 0;
    Core::Instruction::BCTR(*cpu, EncodedInstruction(0x4E800421));
    EXPECT_EQ(cpu->m_gpr[3], 3u) << "OSGetCoreCount ran";
    EXPECT_EQ(cpu->m_lr, 0x404u);
    EXPECT_EQ(cpu->m_nextPc, 0x404u) << "and returned to the caller";

    putString(Name, "");
    EXPECT_NE(call("OSDynLoad_FindExport", {0xD11B0001u, 0, Name, Out}), 0u) << "an empty name is an error";
}

TEST_F(CoreinitExtraTest, DeflateProducesAStreamZlibCanInflateAcrossSeveralCalls)
{
    constexpr std::uint32_t Stream = 0x28340000, In = 0x28341000, Out = 0x28348000;
    std::string save;
    for (int i = 0; i < 200; ++i)
        save += "slot " + std::to_string(i % 7) + " coins=" + std::to_string(i * 13) + ";";
    putString(In, save);
    std::memset(cpu->m_memory.hostPtr(Stream, 0x38), 0, 0x38);
    ASSERT_EQ(call("deflateInit2_", {Stream, 6, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY}), static_cast<std::uint32_t>(Z_OK));
    const auto bound = call("deflateBound", {Stream, static_cast<std::uint32_t>(save.size())});
    ASSERT_GE(bound, 16u);

    // Feed the input in two halves and drain the output through a 64-byte window.
    const auto half = static_cast<std::uint32_t>(save.size() / 2);
    std::vector<unsigned char> compressed;
    int rc = Z_OK;
    for (int part = 0; part < 2 && rc != Z_STREAM_END; ++part) {
        cpu->m_memory.write<std::uint32_t>(Stream + 0x00, In + part * half);
        cpu->m_memory.write<std::uint32_t>(Stream + 0x04, part ? static_cast<std::uint32_t>(save.size()) - half : half);
        do {
            cpu->m_memory.write<std::uint32_t>(Stream + 0x0C, Out);
            cpu->m_memory.write<std::uint32_t>(Stream + 0x10, 64);
            rc = static_cast<int>(call("deflate", {Stream, static_cast<std::uint32_t>(part ? Z_FINISH : Z_NO_FLUSH)}));
            ASSERT_TRUE(rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR) << rc;
            const auto produced = 64 - cpu->m_memory.read<std::uint32_t>(Stream + 0x10);
            const auto *bytes = cpu->m_memory.hostPtr(Out, 64);
            compressed.insert(compressed.end(), bytes, bytes + produced);
        } while (cpu->m_memory.read<std::uint32_t>(Stream + 0x10) == 0 || (part && rc != Z_STREAM_END));
    }
    ASSERT_EQ(rc, Z_STREAM_END);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Stream + 0x08), save.size()) << "total_in";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Stream + 0x14), compressed.size()) << "total_out";
    EXPECT_LT(compressed.size(), save.size() / 2) << "repetitive save data must actually compress";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Stream + 0x30), adler32(1, reinterpret_cast<const Bytef *>(save.data()), save.size()));
    EXPECT_EQ(call("deflateEnd", {Stream}), static_cast<std::uint32_t>(Z_OK));
    EXPECT_EQ(call("deflate", {Stream, Z_FINISH}), static_cast<std::uint32_t>(Z_STREAM_ERROR)) << "an ended stream is gone";

    std::string restored(save.size(), '\0');
    uLongf restoredSize = restored.size();
    ASSERT_EQ(uncompress(reinterpret_cast<Bytef *>(restored.data()), &restoredSize, compressed.data(), compressed.size()), Z_OK);
    EXPECT_EQ(restored, save);
}

TEST_F(CoreinitExtraTest, SharedFontComesFromTheConfiguredFileInEveryGuestMemory)
{
    const auto font = std::filesystem::temp_directory_path() / "wemu-shared-font-test.ttf";
    const std::string bytes = std::string("\x00\x01\x00\x00", 4) + "WEMU test font payload";
    std::ofstream(font, std::ios::binary) << bytes;
    setenv("WEMU_SHARED_FONT", font.c_str(), 1);

    const auto check = [&](Core::Interpreter &target) {
        constexpr std::uint32_t Ptr = 0x28350000, Size = 0x28350004;
        testing::internal::CaptureStderr();
        ASSERT_EQ(call(target, "OSGetSharedData", {2, 0, Ptr, Size}), 1u);
        testing::internal::GetCapturedStderr();
        const auto at = target.m_memory.read<std::uint32_t>(Ptr);
        ASSERT_EQ(target.m_memory.read<std::uint32_t>(Size), bytes.size());
        ASSERT_NE(target.m_memory.hostPtr(at, bytes.size()), nullptr);
        EXPECT_EQ(std::memcmp(target.m_memory.hostPtr(at, bytes.size()), bytes.data(), bytes.size()), 0);
    };
    auto first = std::make_unique<Core::Interpreter>(Core::Binary{});
    check(*first);
    check(*first);
    auto second = std::make_unique<Core::Interpreter>(Core::Binary{});
    check(*second); // its own copy, in its own memory

    unsetenv("WEMU_SHARED_FONT");
    std::filesystem::remove(font);
}

TEST_F(CoreinitExtraTest, ConsoleReportAndPanicReachTheHostLogAndPanicStops)
{
    constexpr std::uint32_t File = 0x28360000, Text = 0x28360100;
    putString(File, "render.c");
    putString(Text, "vertex buffer lost");

    testing::internal::CaptureStderr();
    call("OSConsoleWrite", {Text, 6}); // only the given length
    call("OSVReport", {Text});
    EXPECT_TRUE(cpu->m_running);
    call("OSPanic", {File, 412, Text});
    const auto log = testing::internal::GetCapturedStderr();

    EXPECT_NE(log.find("[OSConsole] vertex\n"), std::string::npos) << log;
    EXPECT_NE(log.find("[OSVReport] vertex buffer lost"), std::string::npos) << log;
    EXPECT_NE(log.find("[OSPanic] render.c:412 vertex buffer lost"), std::string::npos) << log;
    EXPECT_FALSE(cpu->m_running) << "a panic ends the title";
}

TEST_F(CoreinitExtraTest, LibcMemmoveCopiesOverlappingRangesAndReturnsTheDestination)
{
    constexpr std::uint32_t Block = 0x28370000;
    putString(Block, "ABCDEFGH");
    EXPECT_EQ(call("memmove", {Block + 1, Block, 6}), Block + 1);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(cpu->m_memory.hostPtr(Block)), 8), "AABCDEFH");
    EXPECT_EQ(call("memmove", {Block, Block + 1, 6}), Block);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(cpu->m_memory.hostPtr(Block)), 8), "ABCDEFFH");
}

TEST_F(CoreinitExtraTest, PeriodicAlarmFiresOnItsScheduleAndCatchesUpWithoutDrifting)
{
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr std::uint32_t Alarm = 0x28380000, Callback = App + 0x100;
    const auto start = cpu->m_scheduler.now();
    call("OSCreateAlarm", {Alarm});
    // OSSetPeriodicAlarm(alarm, start, interval, callback): 64-bit times in r5:r6 and r7:r8.
    EXPECT_EQ(call("OSSetPeriodicAlarm", {Alarm, 0, 0, static_cast<std::uint32_t>(start + 100), 0, 50, Callback}), 1u);

    const auto advance = [&](std::uint64_t ticks, std::uint32_t pc) {
        cpu->m_pc = pc;
        cpu->m_scheduler.advanceTicks(ticks);
        ProcessCoreinitAlarms(*cpu);
    };
    advance(99, 0x10);
    const auto threadsBefore = cpu->m_scheduler.threads().size();
    Core::Async::OnTick(*cpu);
    EXPECT_EQ(cpu->m_scheduler.threads().size(), threadsBefore) << "not due yet";

    advance(1, 0x20); // t = 100: first fire
    Core::Async::OnTick(*cpu);
    ASSERT_GT(cpu->m_scheduler.threads().size(), threadsBefore);
    const auto &callback = *cpu->m_scheduler.threads().back();
    EXPECT_EQ(callback.pc, Callback - App);
    EXPECT_EQ(callback.gpr[3], Alarm);
    const auto context = callback.gpr[4];
    const auto firedAt = [&] { return cpu->m_memory.read<std::uint32_t>(context + 0x98) - App; };
    EXPECT_EQ(firedAt(), 0x20u) << "the context snapshots where the alarm interrupted";

    advance(49, 0x30); // t = 149
    EXPECT_EQ(firedAt(), 0x20u);
    advance(1, 0x40); // t = 150: second fire
    EXPECT_EQ(firedAt(), 0x40u);

    advance(525, 0x50); // t = 675: missed ten periods, fires once and keeps the 50-tick grid
    EXPECT_EQ(firedAt(), 0x50u);
    advance(24, 0x60); // t = 699
    EXPECT_EQ(firedAt(), 0x50u) << "the next fire is at 700, not 675 + 50";
    advance(1, 0x70); // t = 700
    EXPECT_EQ(firedAt(), 0x70u);

    EXPECT_EQ(call("OSCancelAlarm", {Alarm}), 1u) << "a periodic alarm stays armed after firing";
    advance(100, 0x80);
    EXPECT_EQ(firedAt(), 0x70u) << "cancelled";
    Core::Async::Reset();
}

TEST_F(CoreinitExtraTest, CancellingAnAlarmGroupLeavesOtherGroupsArmed)
{
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr std::uint32_t First = 0x28390000, Second = 0x28390100, Other = 0x28390200;
    for (const auto alarm: {First, Second, Other}) {
        call("OSCreateAlarm", {alarm});
        call("OSSetAlarmTag", {alarm, alarm == Other ? 8u : 7u});
        call("OSSetAlarm", {alarm, 0, 0, 1000, App + 0x100});
    }
    call("OSCancelAlarms", {7});
    EXPECT_EQ(call("OSCancelAlarm", {First}), 0u) << "already cancelled with its group";
    EXPECT_EQ(call("OSCancelAlarm", {Second}), 0u);
    EXPECT_EQ(call("OSCancelAlarm", {Other}), 1u) << "group 8 was still armed";

    // A periodic alarm without an interval would fire forever at one instant: it is never armed.
    call("OSSetPeriodicAlarm", {First, 0, 0, 10, 0, 0, App + 0x100});
    EXPECT_EQ(call("OSCancelAlarm", {First}), 0u);
    Core::Async::Reset();
}

TEST_F(CoreinitExtraTest, DynLoadAllocatorSetByTheTitleIsTheOneItGetsBack)
{
    constexpr std::uint32_t Alloc = 0x28400000, Free = 0x28400004;
    EXPECT_EQ(call("OSDynLoad_SetAllocator", {App + 0x500, App + 0x540}), 0u);
    EXPECT_EQ(call("OSDynLoad_GetAllocator", {Alloc, Free}), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Alloc), App + 0x500);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Free), App + 0x540);
    EXPECT_EQ(call("OSDynLoad_GetAllocator", {0, Free}), 0u) << "either output may be null";
}

TEST_F(CoreinitExtraTest, DataCacheZeroRangeClearsExactlyTheRequestedBytes)
{
    constexpr std::uint32_t Block = 0x28410000;
    for (std::uint32_t i = 0; i < 64; ++i)
        cpu->m_memory.write<std::uint8_t>(Block + i, 0xAB);
    call("DCZeroRange", {Block + 8, 32});
    for (std::uint32_t i = 0; i < 64; ++i)
        EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(Block + i), i >= 8 && i < 40 ? 0u : 0xABu) << i;
    call("DCFlushRange", {Block, 64}); // flush and invalidate have no visible effect
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(Block), 0xABu);
}
