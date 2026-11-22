#include <bit>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

namespace {
    class InputPresenter final : public Core::Gfx::HostPresenter {
        public:
            std::vector<Command> commands;
            Core::Gfx::GamepadState pad;
            std::vector<std::uint32_t> drcFrames; // widths of forwarded GamePad frames
            bool connected() const override { return true; }
            Core::Gfx::GamepadState gamepad() const override { return pad; }
            bool presentDrc(std::span<const std::uint8_t>, std::uint32_t width, std::uint32_t) override
            {
                drcFrames.push_back(width);
                return true;
            }
            std::vector<Command> poll(std::chrono::milliseconds) override { return std::exchange(commands, {}); }
            bool present(std::span<const std::uint8_t>, std::uint32_t, std::uint32_t, const Statistics &) override { return true; }
            void paused(bool, const Statistics &) override {}
    };
} // namespace

// Cooperative scheduler: create/resume/yield/exit and sleep-based wakeups.
class SchedulerTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hle_redirected = false;
            cpu->m_interruptsDisabled = false;
        }
};

static constexpr std::uint32_t APP = Core::Memory::MemoryMap::ApplicationCode;

TEST_F(SchedulerTest, AlarmDeadlinesUseAligned64BitArgumentsAndCancellationPreservesUserData)
{
    RegisterCoreinitExtraFunctions();
    cpu->m_scheduler.bootstrap(*cpu);
    const auto call = [&](const char *name) { Core::syscallHandler.get(name)(*cpu); };
    constexpr unsigned alarm = 0x28001000;
    cpu->m_gpr[3] = alarm;
    call("OSCreateAlarm");
    cpu->m_gpr[3] = alarm;
    cpu->m_gpr[4] = 0x12345678;
    call("OSSetAlarmUserData");
    cpu->m_gpr[3] = alarm;
    cpu->m_gpr[4] = 0xDEADBEEF;
    cpu->m_gpr[5] = 1;
    cpu->m_gpr[6] = 9;
    cpu->m_gpr[7] = APP + 0x100;
    call("OSSetAlarm");
    cpu->m_scheduler.advanceTicks(0x100000008ull);
    ProcessCoreinitAlarms(*cpu);
    cpu->m_gpr[3] = alarm;
    call("OSCancelAlarm");
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    cpu->m_gpr[3] = alarm;
    call("OSGetAlarmUserData");
    EXPECT_EQ(cpu->m_gpr[3], 0x12345678u);
    cpu->m_gpr[3] = alarm;
    cpu->m_gpr[5] = 0;
    cpu->m_gpr[6] = 10;
    cpu->m_gpr[7] = APP + 0x100;
    call("OSSetAlarm");
    cpu->m_scheduler.advanceTicks(10);
    ProcessCoreinitAlarms(*cpu);
    cpu->m_gpr[3] = alarm;
    call("OSCancelAlarm");
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    Core::Async::OnTick(*cpu);
    const auto &threads = cpu->m_scheduler.threads();
    ASSERT_GE(threads.size(), 2u);
    EXPECT_EQ(threads.back()->pc, 0x100u);
    EXPECT_EQ(threads.back()->gpr[3], alarm);
    EXPECT_NE(threads.back()->gpr[4], 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint64_t>(threads.back()->gpr[4]), 0x4F53436F6E747874ull);
    Core::Async::Reset();
}

TEST_F(SchedulerTest, ScreenBuffersAlternateWithoutClearingOrOverwritingTheOtherBuffer)
{
    RegisterCoreinitFunctions();
    const auto call = [&](const char *name, unsigned a, unsigned b = 0, unsigned c = 0, unsigned d = 0) {
        cpu->m_gpr[3] = a;
        cpu->m_gpr[4] = b;
        cpu->m_gpr[5] = c;
        cpu->m_gpr[6] = d;
        Core::syscallHandler.get(name)(*cpu);
        return cpu->m_gpr[3];
    };
    call("OSScreenInit", 0);
    for (unsigned screen = 0; screen < 2; ++screen) {
        const unsigned pitch = screen ? 896 : 1280, height = screen ? 480 : 720;
        const unsigned bytes = pitch * height * 4, base = 0x29000000 + screen * 0x1000000;
        EXPECT_EQ(call("OSScreenGetBufferSizeEx", screen), bytes * 2);
        call("OSScreenSetBufferEx", screen, base);
        call("OSScreenClearBufferEx", screen, 0x11223344);
        call("OSScreenFlipBuffersEx", screen); // flips even with no host renderer
        call("OSScreenClearBufferEx", screen, 0x55667788);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(base), 0x11223344u);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(base + bytes), 0x55667788u);
        call("OSScreenPutPixelEx", screen, 3, 2, 0xAABBCCDD);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(base + bytes + (pitch * 2 + 3) * 4), 0xAABBCCDDu);
        call("OSScreenFlipBuffersEx", screen);
        call("OSScreenPutPixelEx", screen, 3, 2, 0x01020304);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(base + (pitch * 2 + 3) * 4), 0x01020304u);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(base + bytes + (pitch * 2 + 3) * 4), 0xAABBCCDDu);
        // A GamePad padding column cannot be drawn through the visible pixel API.
        if (screen) {
            call("OSScreenPutPixelEx", screen, 854, 2, 0xFFFFFFFF);
            EXPECT_EQ(cpu->m_memory.read<unsigned>(base + (pitch * 2 + 854) * 4), 0x11223344u);
        }
    }
}

TEST_F(SchedulerTest, NestedSuspensionPreservesSleepAndSelfSuspendResumesAtCaller)
{
    RegisterCoreinitFunctions();
    auto &s = cpu->m_scheduler;
    s.bootstrap(*cpu);
    const auto main = s.currentHandle();
    constexpr unsigned worker = 0x28001000;
    auto *t = s.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(s.suspend(*cpu, worker), 1u);
    EXPECT_EQ(s.resume(worker), 2u);
    EXPECT_FALSE(s.yield(*cpu));
    EXPECT_EQ(s.resume(worker), 1u);
    ASSERT_TRUE(s.yield(*cpu));
    ASSERT_EQ(s.currentHandle(), worker);
    cpu->m_lr = 0x1234;
    cpu->m_hle_redirected = false;
    cpu->m_gpr[3] = worker;
    Core::syscallHandler.get("OSSuspendThread")(*cpu);
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(s.currentHandle(), main);
    EXPECT_EQ(t->pc, 0x1234u);
    EXPECT_EQ(t->gpr[3], 0u);
    EXPECT_EQ(s.resume(worker), 1u);
    ASSERT_TRUE(s.yield(*cpu));
    EXPECT_EQ(cpu->m_pc, 0x1234u);
    s.sleep(*cpu, 100);
    EXPECT_EQ(s.suspend(*cpu, worker), 0u);
    EXPECT_EQ(s.resume(worker), 1u);
    EXPECT_EQ(t->state, Core::ThreadContext::State::Sleeping);
    EXPECT_EQ(s.resume(worker), 0u); // redundant resume must not wake a sleeper
    EXPECT_EQ(t->state, Core::ThreadContext::State::Sleeping);
    s.advanceTicks(100);
    EXPECT_EQ(t->state, Core::ThreadContext::State::Ready);
}

TEST_F(SchedulerTest, ThreadQueriesDistinguishSuspensionFromSleepAndTrackAffinityAndReuse)
{
    RegisterCoreinitFunctions();
    auto &scheduler = cpu->m_scheduler;
    scheduler.bootstrap(*cpu);
    constexpr unsigned worker = 0x28001000;
    const auto query = [&](const char *name, unsigned handle) {
        cpu->m_gpr[3] = handle;
        Core::syscallHandler.get(name)(*cpu);
        return cpu->m_gpr[3];
    };
    EXPECT_EQ(query("OSIsThreadSuspended", scheduler.mainHandle()), 0u);
    EXPECT_EQ(query("OSGetThreadAffinity", scheduler.mainHandle()), 2u);
    EXPECT_EQ(query("OSIsThreadSuspended", 0), 0u);
    EXPECT_EQ(query("OSGetThreadAffinity", 0xFFFFFF00), 0u);
    ASSERT_NE(scheduler.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000), nullptr);
    EXPECT_EQ(query("OSIsThreadSuspended", worker), 1u);
    scheduler.setAffinity(worker, 5);
    EXPECT_EQ(query("OSGetThreadAffinity", worker), 5u);
    scheduler.resume(worker);
    EXPECT_EQ(query("OSIsThreadSuspended", worker), 0u);
    ASSERT_TRUE(scheduler.yield(*cpu));
    ASSERT_EQ(scheduler.currentHandle(), worker);
    EXPECT_EQ(query("OSIsThreadSuspended", worker), 0u);
    scheduler.sleep(*cpu, 100);
    EXPECT_EQ(query("OSIsThreadSuspended", worker), 0u); // sleeping is not suspended
    scheduler.advanceTicks(100);
    ASSERT_TRUE(scheduler.yield(*cpu));
    ASSERT_EQ(scheduler.currentHandle(), worker);
    ASSERT_TRUE(scheduler.exitCurrent(*cpu));
    EXPECT_EQ(query("OSIsThreadSuspended", worker), 0u);
    ASSERT_NE(scheduler.create(*cpu, worker, APP + 0x200, 0, 0, 0x28010000), nullptr);
    EXPECT_EQ(query("OSIsThreadSuspended", worker), 1u);
    EXPECT_EQ(query("OSGetThreadAffinity", worker), 2u);
}

TEST_F(SchedulerTest, AtomicAddAndSwapReturnPreviousValuesAndWrapAt32Bits)
{
    RegisterCoreinitFunctions();
    constexpr unsigned address = 0x28001000;
    const auto add = [&](unsigned initial, unsigned increment, unsigned expected) {
        cpu->m_memory.write<unsigned>(address, initial);
        cpu->m_memory.write<unsigned>(address + 4, 0xCAFEBABE);
        cpu->m_gpr[3] = address;
        cpu->m_gpr[4] = increment;
        Core::syscallHandler.get("OSAddAtomic")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], initial);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(address), expected);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(address + 4), 0xCAFEBABEu);
    };
    add(7, static_cast<unsigned>(-3), 4);
    add(0x7FFFFFFF, 1, 0x80000000);
    add(0xFFFFFFFF, 1, 0);
    cpu->m_gpr[3] = address;
    cpu->m_gpr[4] = 0x12345678;
    Core::syscallHandler.get("OSSwapAtomic")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(address), 0x12345678u);
    cpu->m_gpr[3] = address;
    cpu->m_gpr[4] = 0x87654321;
    Core::syscallHandler.get("OSSwapAtomic")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0x12345678u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(address), 0x87654321u);
}

TEST_F(SchedulerTest, OfflineSocketCallsFailWithoutInventingDescriptorsOrModifyingReceiveBuffers)
{
    RegisterCoreinitFunctions();
    constexpr unsigned buffer = 0x28001000, length = 0x28001100;
    cpu->m_memory.write<unsigned>(buffer, 0x12345678);
    cpu->m_memory.write<unsigned>(length, 16);
    Core::syscallHandler.get("socket_lib_init")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    for (const char *name:
         {"socket", "connect", "bind", "listen", "accept", "send", "sendto", "recv", "recvfrom", "setsockopt", "shutdown", "socketclose"}) {
        SCOPED_TRACE(name);
        cpu->m_gpr[3] = 2;
        cpu->m_gpr[4] = buffer;
        cpu->m_gpr[5] = length;
        Core::syscallHandler.get(name)(*cpu);
        EXPECT_EQ(static_cast<std::int32_t>(cpu->m_gpr[3]), -1);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(buffer), 0x12345678u);
        EXPECT_EQ(cpu->m_memory.read<unsigned>(length), 16u);
    }
}

TEST_F(SchedulerTest, ThreadTerminationQueryTracksCreationExitAndHandleReuse)
{
    RegisterCoreinitFunctions();
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr unsigned worker = 0x28001000;
    const auto terminated = [&](unsigned handle) {
        cpu->m_gpr[3] = handle;
        Core::syscallHandler.get("OSIsThreadTerminated")(*cpu);
        return cpu->m_gpr[3];
    };
    cpu->m_memory.write<std::uint8_t>(worker + 0x324, 0);
    EXPECT_EQ(terminated(worker), 1u); // inactive OSThread, before creation
    EXPECT_EQ(terminated(cpu->m_scheduler.mainHandle()), 0u);
    EXPECT_EQ(terminated(0), 0u);
    EXPECT_EQ(terminated(0xFFFFFF00), 0u);
    auto *thread = cpu->m_scheduler.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000);
    ASSERT_NE(thread, nullptr);
    EXPECT_EQ(terminated(worker), 0u); // suspended is not terminated
    cpu->m_scheduler.resume(worker);
    EXPECT_EQ(terminated(worker), 0u);
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    EXPECT_EQ(terminated(worker), 0u);
    cpu->m_gpr[3] = 42;
    ASSERT_TRUE(cpu->m_scheduler.exitCurrent(*cpu));
    EXPECT_EQ(terminated(worker), 1u); // observable before OSJoinThread
    EXPECT_NE(cpu->m_scheduler.create(*cpu, worker, APP + 0x200, 0, 0, 0x28010000), nullptr);
    EXPECT_EQ(terminated(worker), 0u);
}

TEST_F(SchedulerTest, GamepadLcdModeInitializesOutputAndTracksPowerChanges)
{
    RegisterCoreinitFunctions();
    constexpr unsigned output = 0x28001000;
    Core::syscallHandler.get("VPADInit")(*cpu);
    const auto read = [&] {
        cpu->m_memory.write<unsigned>(output, 0x12345678);
        cpu->m_gpr[3] = 0;
        cpu->m_gpr[4] = output;
        Core::syscallHandler.get("VPADGetLcdMode")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        return cpu->m_memory.read<unsigned>(output);
    };
    EXPECT_EQ(read(), 0xFFu);
    for (const unsigned mode: {0u, 1u, 0xFFu}) {
        cpu->m_gpr[3] = 0;
        cpu->m_gpr[4] = mode;
        Core::syscallHandler.get("VPADSetLcdMode")(*cpu);
        ASSERT_EQ(cpu->m_gpr[3], 0u);
        EXPECT_EQ(read(), mode);
    }
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[4] = output;
    Core::syscallHandler.get("VPADGetLcdMode")(*cpu);
    EXPECT_NE(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(output), 0xFFu);
}

TEST_F(SchedulerTest, NewThreadsStartWithCafeQuantizationDefaultsAndPreserveChanges)
{
    constexpr unsigned worker = 0x28001000;
    const std::array<unsigned, 8> defaults{0, 0, 0x00040004, 0x00050005, 0x00060006, 0x00070007, 0, 0};
    cpu->m_scheduler.bootstrap(*cpu);
    EXPECT_TRUE(std::equal(defaults.begin(), defaults.end(), cpu->m_gqr));
    cpu->m_gqr[3] = 0x05050505;
    auto *thread = cpu->m_scheduler.create(*cpu, worker, APP + 0x200, 0, 0, 0x28010000, 16);
    ASSERT_NE(thread, nullptr);
    EXPECT_TRUE(std::equal(defaults.begin(), defaults.end(), thread->gqr));
    cpu->m_scheduler.resume(worker);
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    EXPECT_TRUE(std::equal(defaults.begin(), defaults.end(), cpu->m_gqr));
    // psq_st f1,0(r3),1,3 stores an unsigned halfword, rather than float bytes.
    cpu->m_gpr[3] = 0x28020000;
    cpu->m_fpr[1] = 41949.f;
    cpu->executeInstruction(EncodedInstruction{(60u << 26) | (1u << 21) | (3u << 16) | (1u << 15) | (3u << 12)});
    EXPECT_EQ(cpu->m_memory.read<unsigned short>(0x28020000), 41949u);
    cpu->m_memory.write<unsigned short>(0x28020000, 9181);
    // psq_l f2,0(r3),1,5 reads a signed halfword used by quantized animation curves.
    cpu->executeInstruction(EncodedInstruction{(56u << 26) | (2u << 21) | (3u << 16) | (1u << 15) | (5u << 12)});
    EXPECT_EQ(cpu->m_fpr[2], 9181.);
    cpu->m_gqr[5] = 0x02070207;
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    EXPECT_EQ(cpu->m_gqr[3], 0x05050505u);
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    EXPECT_EQ(cpu->m_gqr[5], 0x02070207u);
}

TEST_F(SchedulerTest, Atomic64UsesAlignedArgumentsAndBothReturnRegisters)
{
    RegisterCoreinitFunctions();
    constexpr unsigned address = 0x28001000;
    cpu->m_memory.write<std::uint64_t>(address, 0x1234567887654321ULL);
    cpu->m_gpr[3] = address;
    cpu->m_gpr[4] = 0xDEADBEEF; // ABI padding, not part of the new value
    cpu->m_gpr[5] = 0xABCDEF12;
    cpu->m_gpr[6] = 0x34567890;
    Core::syscallHandler.get("OSSetAtomic64")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0x12345678u);
    EXPECT_EQ(cpu->m_gpr[4], 0x87654321u);
    EXPECT_EQ(cpu->m_memory.read<std::uint64_t>(address), 0xABCDEF1234567890ULL);
    cpu->m_gpr[3] = address;
    Core::syscallHandler.get("OSGetAtomic64")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0xABCDEF12u);
    EXPECT_EQ(cpu->m_gpr[4], 0x34567890u);
}

TEST_F(SchedulerTest, JoinDeliversWorkerReturnValueForWaitingAndFinishedThreads)
{
    RegisterCoreinitFunctions();
    constexpr unsigned worker = 0x28001000, result = 0x28002000;
    cpu->m_scheduler.bootstrap(*cpu);
    const auto main = cpu->m_scheduler.mainHandle();
    ASSERT_NE(cpu->m_scheduler.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000), nullptr);
    cpu->m_scheduler.resume(worker);
    cpu->m_gpr[3] = worker;
    cpu->m_gpr[4] = result;
    Core::syscallHandler.get("OSJoinThread")(*cpu);
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), worker);
    cpu->m_gpr[3] = 0x54321000; // pointer returned by the worker's entry function
    ASSERT_TRUE(cpu->m_scheduler.exitCurrent(*cpu));
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), main);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(result), 0x54321000u);
    cpu->m_hle_redirected = false;
    cpu->m_gpr[3] = worker;
    cpu->m_gpr[4] = result + 4;
    Core::syscallHandler.get("OSJoinThread")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(result + 4), 0x54321000u);
}

TEST_F(SchedulerTest, SemaphoreWaitBlocksUntilSignalThenConsumesOnePermit)
{
    RegisterCoreinitFunctions();
    constexpr unsigned sem = 0x28001000, worker = 0x28002000;
    cpu->m_scheduler.bootstrap(*cpu);
    const auto main = cpu->m_scheduler.mainHandle();
    cpu->m_gpr[3] = sem;
    cpu->m_gpr[4] = 0;
    Core::syscallHandler.get("OSInitSemaphore")(*cpu);
    ASSERT_NE(cpu->m_scheduler.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000), nullptr);
    cpu->m_scheduler.resume(worker);
    cpu->m_gpr[3] = sem;
    Core::syscallHandler.get("OSWaitSemaphore")(*cpu);
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), worker);
    ASSERT_EQ(cpu->m_scheduler.find(main)->waitKey, sem);
    cpu->m_gpr[3] = sem;
    Core::syscallHandler.get("OSSignalSemaphore")(*cpu);
    if (cpu->m_scheduler.currentHandle() != main)
        ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), main);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(sem + 12), 1u);
    cpu->m_gpr[3] = sem;
    Core::syscallHandler.get("OSWaitSemaphore")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(sem + 12), 0u);
    cpu->m_gpr[3] = sem;
    Core::syscallHandler.get("OSTryWaitSemaphore")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
}

TEST_F(SchedulerTest, OfflineFriendListInitializesOutputCount)
{
    RegisterCoreinitFunctions();
    constexpr unsigned list = 0x28001000, count = 0x28002000;
    cpu->m_memory.write<unsigned>(count, 0xFFFFFFFF);
    cpu->m_memory.write<unsigned>(list, 0x12345678);
    cpu->m_gpr[3] = list;
    cpu->m_gpr[4] = count;
    cpu->m_gpr[5] = 0;
    cpu->m_gpr[6] = 100;
    Core::syscallHandler.get("GetFriendList__Q2_2nn2fpFPUiT1UiT3")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(count), 0u);
    EXPECT_EQ(cpu->m_memory.read<unsigned>(list), 0x12345678u);
}

TEST_F(SchedulerTest, ConditionWaitReleasesAndRestoresRecursiveMutexOwnership)
{
    RegisterCoreinitFunctions();
    constexpr unsigned condition = 0x28001000, mutex = 0x28002000, worker = 0x28003000;
    cpu->m_scheduler.bootstrap(*cpu);
    const auto main = cpu->m_scheduler.mainHandle();
    ASSERT_TRUE(cpu->m_scheduler.mutexLock(*cpu, mutex));
    ASSERT_TRUE(cpu->m_scheduler.mutexLock(*cpu, mutex));
    ASSERT_NE(cpu->m_scheduler.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000), nullptr);
    cpu->m_scheduler.resume(worker);
    cpu->m_gpr[3] = condition;
    cpu->m_gpr[4] = mutex;
    Core::syscallHandler.get("OSWaitCond")(*cpu);
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), worker);
    ASSERT_TRUE(cpu->m_scheduler.mutexTryLock(mutex)); // sleeping main released both levels
    cpu->m_gpr[3] = condition;
    Core::syscallHandler.get("OSSignalCond")(*cpu);
    cpu->m_scheduler.mutexUnlock(mutex);
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), main);
    cpu->m_gpr[3] = condition;
    cpu->m_gpr[4] = mutex;
    Core::syscallHandler.get("OSWaitCond")(*cpu); // resume the blocked HLE instruction
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_scheduler.mutexUnlock(mutex); // one recursive level remains
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), worker);
    EXPECT_FALSE(cpu->m_scheduler.mutexTryLock(mutex));
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    cpu->m_scheduler.mutexUnlock(mutex);
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    EXPECT_TRUE(cpu->m_scheduler.mutexTryLock(mutex));
    cpu->m_scheduler.mutexUnlock(mutex);
}

TEST_F(SchedulerTest, QuantizedPairedSinglesApplyPositiveAndNegativeScales)
{
    cpu->m_gpr[3] = 0x28020000;
    // GQR1: store type 7 (s16), load type 7, scale 3 (x8 on store, /8 on load).
    cpu->m_gqr[1] = (3u << 24) | (7u << 16) | (3u << 8) | 7u;
    cpu->m_fpr[1] = -12.5;
    cpu->m_ps1[1] = 2.25;
    // psq_st f1,0(r3),0,1 then psq_l f2,0(r3),0,1.
    cpu->executeInstruction(EncodedInstruction{(60u << 26) | (1u << 21) | (3u << 16) | (1u << 12)});
    EXPECT_EQ(std::int16_t(cpu->m_memory.read<unsigned short>(0x28020000)), -100);
    EXPECT_EQ(std::int16_t(cpu->m_memory.read<unsigned short>(0x28020002)), 18);
    cpu->executeInstruction(EncodedInstruction{(56u << 26) | (2u << 21) | (3u << 16) | (1u << 12)});
    EXPECT_EQ(cpu->m_fpr[2], -12.5);
    EXPECT_EQ(cpu->m_ps1[2], 2.25);
    // A six-bit scale of 0x3E is -2: stores divide by 4, loads multiply by 4 (u8).
    cpu->m_gqr[1] = (0x3Eu << 24) | (4u << 16) | (0x3Eu << 8) | 4u;
    cpu->m_fpr[1] = 400;
    cpu->m_ps1[1] = 8;
    cpu->executeInstruction(EncodedInstruction{(60u << 26) | (1u << 21) | (3u << 16) | (1u << 12)});
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(0x28020000), 100u);
    EXPECT_EQ(cpu->m_memory.read<std::uint8_t>(0x28020001), 2u);
    cpu->executeInstruction(EncodedInstruction{(56u << 26) | (2u << 21) | (3u << 16) | (1u << 12)});
    EXPECT_EQ(cpu->m_fpr[2], 400.);
    EXPECT_EQ(cpu->m_ps1[2], 8.);
}

TEST_F(SchedulerTest, VpadReadInitializesOneCompleteSampleAndTracksEdges)
{
    RegisterCoreinitFunctions();
    constexpr unsigned BUF = 0x28001000, ERR = 0x28002000;
    const auto read = [&](unsigned channel = 0, unsigned count = 2) {
        cpu->m_gpr[3] = channel;
        cpu->m_gpr[4] = BUF;
        cpu->m_gpr[5] = count;
        cpu->m_gpr[6] = ERR;
        Core::syscallHandler.get("VPADRead")(*cpu);
    };
    Core::syscallHandler.get("VPADInit")(*cpu);
    for (unsigned i = 0; i < 0x158; i += 4)
        cpu->m_memory.write<std::uint32_t>(BUF + i, 0xDEADBEEF);
    cpu->m_controllerMask = 0x8800;
    read();
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(ERR), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF), 0x8800u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + 4), 0x8800u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + 8), 0u);
    // Neutral GamePad: centred sticks, gravity along -y, no touch (both axes invalid), identity orientation.
    const auto neutral = [](unsigned offset) -> std::uint32_t {
        switch (offset) {
            case 0x20: // acc.y
            case 0x34: // vertical.y
                return 0xBF800000u;
            case 0x28: // acc magnitude
            case 0x6C:
            case 0x7C:
            case 0x8C:
                return 0x3F800000u;
            case 0x58: // tpNormal / tpFiltered1 / tpFiltered2 validity
            case 0x60:
            case 0x68:
                return 0x00030000u;
            default:
                return 0u;
        }
    };
    for (unsigned i = 12; i < 0xAC; i += 4)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + i), neutral(i)) << "offset 0x" << std::hex << i;
    for (unsigned i = 0xAC; i < 0x158; i += 4)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + i), 0xDEADBEEFu);
    read();
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + 4), 0u);
    cpu->m_controllerMask = 0x4000;
    read();
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + 4), 0x4000u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + 8), 0x8800u);
    read(1);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(ERR), -2u);
    read(0, 0);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(ERR), -1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF), 0x4000u);
    Core::syscallHandler.get("VPADInit")(*cpu);
    read();
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + 4), 0x4000u);
}

TEST_F(SchedulerTest, VpadReadObservesPressAndReleaseReceivedBetweenGuestPolls)
{
    RegisterCoreinitFunctions();
    cpu->m_controllerMask = 0;
    Core::syscallHandler.get("VPADInit")(*cpu);
    auto presenter = std::make_unique<InputPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;
    constexpr unsigned buffer = 0x28001000, error = 0x28002000;
    const auto read = [&] {
        cpu->m_gpr[3] = 0;
        cpu->m_gpr[4] = buffer;
        cpu->m_gpr[5] = 1;
        cpu->m_gpr[6] = error;
        Core::syscallHandler.get("VPADRead")(*cpu);
    };
    using Action = Core::Gfx::HostPresenter::Action;
    transport->commands = {{Action::Buttons, 0x8000}, {Action::Buttons, 0}};
    read();
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer), 0x8000u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer + 4), 0x8000u);
    read();
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer + 8), 0x8000u);
    read();
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer + 4), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer + 8), 0u);
    cpu->m_renderer = nullptr;
}

TEST_F(SchedulerTest, VpadReadReportsRemoteGamepadSticksTouchAndMotion)
{
    RegisterCoreinitFunctions();
    cpu->m_controllerMask = 0;
    Core::syscallHandler.get("VPADInit")(*cpu);
    auto presenter = std::make_unique<InputPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;
    constexpr unsigned buffer = 0x28001000, error = 0x28002000, calibrated = 0x28003000;
    transport->pad.leftStick = {-0.75f, 0.25f};
    transport->pad.rightStick = {0.0f, 1.0f};
    transport->pad.touched = true;
    transport->pad.touchX = 853;
    transport->pad.touchY = 240;
    transport->pad.gyro = {0.5f, 0.0f, -0.25f};
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = buffer;
    cpu->m_gpr[5] = 1;
    cpu->m_gpr[6] = error;
    Core::syscallHandler.get("VPADRead")(*cpu);
    const auto f32 = [&](unsigned offset) { return std::bit_cast<float>(cpu->m_memory.read<std::uint32_t>(buffer + offset)); };
    EXPECT_FLOAT_EQ(f32(0x0C), -0.75f);
    EXPECT_FLOAT_EQ(f32(0x10), 0.25f);
    EXPECT_FLOAT_EQ(f32(0x18), 1.0f);
    EXPECT_FLOAT_EQ(f32(0x38), 0.5f);
    EXPECT_FLOAT_EQ(f32(0x40), -0.25f);
    // Stick emulation bits: left stick left, right stick up.
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(buffer), 0x40000000u | 0x01000000u);
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(buffer + 0x56), 1u); // touched
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(buffer + 0x58), 0u); // valid
    // The raw panel sample converts back to DRC pixels through the calibration HLE.
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = calibrated;
    cpu->m_gpr[5] = buffer + 0x52;
    Core::syscallHandler.get("VPADGetTPCalibratedPoint")(*cpu);
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(calibrated), 853u);
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(calibrated + 2), 240u);
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(calibrated + 4), 1u);
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = 1; // 1280x720
    cpu->m_gpr[5] = calibrated;
    cpu->m_gpr[6] = buffer + 0x52;
    Core::syscallHandler.get("VPADGetTPCalibratedPointEx")(*cpu);
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(calibrated), 1279u);
    EXPECT_NEAR(cpu->m_memory.read<std::uint16_t>(calibrated + 2), 360, 1);
    cpu->m_renderer = nullptr;
}

TEST_F(SchedulerTest, GamepadFramesAreForwardedOnlyWhileTheHostStreamsThem)
{
    auto presenter = std::make_unique<InputPresenter>();
    auto *transport = presenter.get();
    Renderer renderer(std::move(presenter));
    const std::vector<std::uint8_t> frame(854 * 480 * 4);
    renderer.flip_drc(frame.data(), 854, 480);
    EXPECT_FALSE(renderer.drc_wanted());
    using Action = Core::Gfx::HostPresenter::Action;
    transport->commands = {{Action::DrcStream, 1}};
    renderer.poll_events();
    EXPECT_TRUE(renderer.drc_wanted());
    renderer.flip_drc(frame.data(), 854, 480);
    transport->commands = {{Action::DrcStream, 0}};
    renderer.poll_events();
    renderer.flip_drc(frame.data(), 854, 480);
    EXPECT_EQ(transport->drcFrames, std::vector<std::uint32_t>{854});
}

TEST_F(SchedulerTest, OsClockReadsShareSchedulerTimeWithoutAdvancingIt)
{
    RegisterCoreinitFunctions();
    constexpr std::uint64_t ticks = 0x123456789ull;
    cpu->m_scheduler.advanceTicks(ticks);
    for (int repeat = 0; repeat < 3; ++repeat) {
        Core::syscallHandler.get("OSGetTick")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], static_cast<std::uint32_t>(ticks));
        for (const auto *name: {"OSGetTime", "OSGetSystemTime"}) {
            Core::syscallHandler.get(name)(*cpu);
            EXPECT_EQ(cpu->m_gpr[3], ticks >> 32);
            EXPECT_EQ(cpu->m_gpr[4], static_cast<std::uint32_t>(ticks));
        }
        EXPECT_EQ(cpu->m_scheduler.now(), ticks);
    }
    cpu->m_scheduler = Core::Scheduler{};
    Core::syscallHandler.get("OSGetTime")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

TEST_F(SchedulerTest, FrameClockAccountsForExecutionAndWaitTime)
{
    auto &sch = cpu->m_scheduler;
    sch.advanceTicks(5000); // long startup before the first present
    sch.advanceFrame(100);
    EXPECT_EQ(sch.now(), 5000u);
    sch.advanceTicks(40);
    sch.advanceFrame(100);
    EXPECT_EQ(sch.now(), 5100u);
    sch.advanceTicks(100); // a completed vsync wait must not count twice
    sch.advanceFrame(100);
    EXPECT_EQ(sch.now(), 5200u);
    sch.advanceFrame(100);
    EXPECT_EQ(sch.now(), 5300u);
    sch.advanceTicks(UINT64_MAX);
    sch.advanceFrame(100);
    EXPECT_EQ(sch.now(), UINT64_MAX);
}

TEST_F(SchedulerTest, FrameClockWakesTimedWaiters)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto main = sch.currentHandle();
    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.blockWithTimeout(*cpu, 0x1234, 100));
    EXPECT_EQ(sch.find(main)->state, Core::ThreadContext::State::Waiting);
    sch.advanceFrame(100);
    EXPECT_EQ(sch.find(main)->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(sch.find(main)->gpr[3], 0u);
}

TEST_F(SchedulerTest, FrameClockDoesNotAccumulatePresentCompletionJitter)
{
    auto &sch = cpu->m_scheduler;
    sch.advanceFrame(100);
    for (unsigned pair = 0; pair < 1000; ++pair) {
        sch.advanceTicks(110); // work/wait completes ten ticks past this deadline
        sch.advanceFrame(100);
        sch.advanceTicks(40); // next frame has room to absorb the late completion
        sch.advanceFrame(100);
        EXPECT_EQ(sch.now(), 100u + (pair + 1) * 200u);
    }
}

TEST_F(SchedulerTest, FrameWaitRunsMovieWorkersBeforeAdvancingToDisplayDeadline)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto main = sch.currentHandle();
    constexpr unsigned worker = 0x28001000;
    sch.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000);
    sch.resume(worker);
    sch.waitForFrame(*cpu, 100);
    EXPECT_EQ(sch.currentHandle(), worker);
    EXPECT_EQ(sch.now(), 0u); // a ready producer gets CPU time before the clock jumps
    EXPECT_EQ(sch.find(main)->state, Core::ThreadContext::State::Sleeping);
    sch.advanceTicks(40);
    ASSERT_TRUE(sch.yield(*cpu));
    EXPECT_EQ(sch.now(), 100u); // producer yielded; only the display waiter remains
    EXPECT_EQ(sch.currentHandle(), main);
    sch.advanceTicks(30);
    sch.waitForFrame(*cpu, 100);
    EXPECT_EQ(sch.find(main)->wakeTick, 200u); // work already spent counts toward this frame
    EXPECT_EQ(sch.currentHandle(), worker);
}

TEST_F(SchedulerTest, YieldPollingAllowsHigherPriorityTimeoutToExpire)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto main = sch.currentHandle();
    constexpr unsigned worker = 0x28001000;
    sch.create(*cpu, worker, APP + 0x100, 0, 0, 0x28010000, 1);
    sch.resume(worker);
    ASSERT_TRUE(sch.yield(*cpu));
    ASSERT_EQ(sch.currentHandle(), worker);
    ASSERT_TRUE(sch.blockWithTimeout(*cpu, 0x1234, 1000));
    ASSERT_EQ(sch.currentHandle(), main);
    for (unsigned attempt = 0; attempt < 1000 && sch.currentHandle() == main; ++attempt)
        Core::syscallHandler.get("OSYieldThread")(*cpu);
    EXPECT_EQ(sch.currentHandle(), worker);
    EXPECT_GE(sch.now(), 1000u);
    EXPECT_EQ(cpu->m_gpr[3], 0u); // timeout, not a signal
}

TEST_F(SchedulerTest, FatalMainThreadFaultMarksSessionFailed)
{
    cpu->m_scheduler.bootstrap(*cpu);
    cpu->m_running = true;
    Core::Interpreter::Block block;
    block.instrs.push_back({Core::Instruction::STW, EncodedInstruction(0)});
    cpu->runBlock(block); // r0=0: store through the unmapped null address
    EXPECT_TRUE(cpu->failed());
    EXPECT_FALSE(cpu->m_running);
    cpu->reset();
    EXPECT_FALSE(cpu->failed());
}

TEST_F(SchedulerTest, SlowInterpreterFaultMarksSessionFailed)
{
    cpu->m_scheduler.bootstrap(*cpu);
    Utils::BeDecoder store(std::vector<char>{char(0x90), 0, 0, 0}); // stw r0,0(0)
    EXPECT_FALSE(cpu->step(store, APP));
    EXPECT_TRUE(cpu->failed());
    cpu->reset();
    Utils::BeDecoder illegal(std::vector<char>(4, 0));
    EXPECT_FALSE(cpu->step(illegal, APP));
    EXPECT_TRUE(cpu->failed());
}

TEST_F(SchedulerTest, SecondaryThreadFaultDoesNotReportWholeSessionStopped)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto main = sch.currentHandle();
    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.yield(*cpu));
    cpu->m_running = true;
    Core::Interpreter::Block block;
    block.instrs.push_back({Core::Instruction::STW, EncodedInstruction(0)});
    cpu->runBlock(block);
    EXPECT_FALSE(cpu->failed());
    EXPECT_TRUE(cpu->m_running);
    EXPECT_EQ(sch.currentHandle(), main);
}

TEST_F(SchedulerTest, RendezvousWaitsForWorkerInitialization)
{
    RegisterCoreinitExtraFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto main = sch.currentHandle();
    constexpr std::uint32_t barrier = 0x10002000;
    cpu->m_memory.write<std::uint32_t>(barrier + 12, 0x12345678);
    cpu->m_gpr[3] = barrier;
    Core::syscallHandler.get("OSInitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier + 12), 0x12345678u);
    sch.create(*cpu, 0x2000, APP + 0x200, 7, 0, 0xC0000000);
    sch.setAffinity(0x2000, 1);
    sch.resume(0x2000);
    cpu->m_pc = 0x500;
    cpu->m_lr = 0x600;
    cpu->m_gpr[3] = barrier;
    cpu->m_gpr[4] = 1;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    EXPECT_EQ(cpu->m_gpr[3], 7u);
    EXPECT_EQ(sch.find(main)->state, Core::ThreadContext::State::Waiting);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier + 4), 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier), 0u);
    cpu->m_gpr[3] = barrier;
    cpu->m_gpr[4] = 3;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    EXPECT_EQ(cpu->m_pc, 0x500u);
    EXPECT_EQ(cpu->m_gpr[3], barrier);
    EXPECT_EQ(cpu->m_gpr[4], 1u);
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier), 1u);
    cpu->m_gpr[3] = barrier;
    Core::syscallHandler.get("OSInitRendezvous")(*cpu);
    for (unsigned core = 0; core < 3; ++core)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier + core * 4), 0u);
}

TEST_F(SchedulerTest, RendezvousDoesNotInventMissingCoreArrivals)
{
    RegisterCoreinitExtraFunctions();
    cpu->m_scheduler.bootstrap(*cpu);
    constexpr std::uint32_t barrier = 0x10002000;
    cpu->m_gpr[3] = barrier;
    Core::syscallHandler.get("OSInitRendezvous")(*cpu);
    cpu->m_pc = 0x500;
    cpu->m_nextPc = 0x504;
    cpu->m_gpr[4] = 7;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], barrier);
    EXPECT_EQ(cpu->m_nextPc, 0x500u);
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier), 0u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier + 8), 0u);
    // An empty mask succeeds, but still records the calling core's arrival.
    cpu->m_gpr[4] = 0;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(barrier + 4), 1u);
}

TEST_F(SchedulerTest, RendezvousRechecksMaskAfterPartialArrival)
{
    RegisterCoreinitExtraFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    constexpr std::uint32_t barrier = 0x10002000;
    cpu->m_gpr[3] = barrier;
    Core::syscallHandler.get("OSInitRendezvous")(*cpu);
    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.setAffinity(0x2000, 1);
    sch.resume(0x2000);
    cpu->m_pc = 0x500;
    cpu->m_gpr[4] = 7;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    cpu->m_gpr[3] = barrier;
    cpu->m_gpr[4] = 3;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    cpu->m_hle_redirected = false;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], barrier);
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_nextPc, 0x500u);
    cpu->m_memory.write<std::uint32_t>(barrier + 8, 1);
    cpu->m_hle_redirected = false;
    Core::syscallHandler.get("OSWaitRendezvous")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_FALSE(cpu->m_hle_redirected);
}

TEST_F(SchedulerTest, YieldSwitchesContextAndExitReturns)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);

    // Mark the main thread's state and give it a resume point in LR.
    cpu->m_gpr[10] = 0xCAFE0001u;
    cpu->m_lr = 0x00005000u; // main resumes here (offset-space) after the yield
    const std::uint32_t mainHandle = sch.currentHandle();

    // Create + resume a worker thread with entry 0x02000100, argc=7.
    const std::uint32_t entry = APP + 0x100;
    sch.create(*cpu, /*osThread=*/0x1000u, entry, /*argc=*/7u, /*argv=*/0u, /*stackTop=*/0xC0000000u);
    sch.resume(0x1000u);

    // Yield from main -> worker.
    ASSERT_TRUE(sch.yield(*cpu));
    EXPECT_EQ(sch.currentHandle(), 0x1000u);
    EXPECT_EQ(cpu->m_pc, 0x100u); // worker entry, offset-space
    EXPECT_EQ(cpu->m_gpr[3], 7u); // argc
    EXPECT_TRUE(cpu->m_hle_redirected);

    // Worker finishes -> should switch back to the (Ready) main thread and restore its registers.
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    EXPECT_EQ(sch.currentHandle(), mainHandle);
    EXPECT_EQ(cpu->m_gpr[10], 0xCAFE0001u); // main's saved register restored
    EXPECT_EQ(cpu->m_pc, 0x5000u); // main resumes at its saved LR
}

TEST_F(SchedulerTest, YieldWithNoOtherThreadIsNoOp)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const std::uint32_t before = sch.currentHandle();
    EXPECT_FALSE(sch.yield(*cpu)); // nobody else runnable
    EXPECT_EQ(sch.currentHandle(), before);
}

TEST_F(SchedulerTest, YieldLetsTheLocalControllerResumeWhileOtherCoresHaveBusyWorkers)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto controller = sch.currentHandle();
    sch.setAffinity(controller, 1);
    sch.setPriority(controller, 17);
    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000, 16);
    sch.setAffinity(0x2000, 1);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.rescheduleAfterHle(*cpu));
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    // An unrelated worker on core 1 has a better priority than the controller
    // on core 0. It must not take every explicit yield away from that controller.
    sch.create(*cpu, 0x3000, APP + 0x300, 0, 0, 0xC0010000, 16);
    sch.setAffinity(0x3000, 2);
    sch.resume(0x3000);
    cpu->m_lr = 0x440;
    ASSERT_TRUE(sch.yield(*cpu));
    EXPECT_EQ(sch.currentHandle(), controller);
    // Other cores still receive instruction-boundary slices.
    ASSERT_TRUE(sch.preempt(*cpu));
    EXPECT_NE(sch.currentHandle(), controller);
}

TEST_F(SchedulerTest, FinishedThreadHandleCanBeRecreatedAndResumed)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    auto *first = sch.create(*cpu, 0x2000, APP + 0x200, 7, 0, 0xC0000000);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.yield(*cpu));
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    EXPECT_EQ(first->state, Core::ThreadContext::State::Finished);
    auto *second = sch.create(*cpu, 0x2000, APP + 0x400, 9, 0x28001000, 0xC0010000, 10);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(sch.threads().size(), 2u);
    EXPECT_EQ(second->state, Core::ThreadContext::State::Paused);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.rescheduleAfterHle(*cpu));
    EXPECT_EQ(sch.currentHandle(), 0x2000u);
    EXPECT_EQ(cpu->m_pc, 0x400u);
    EXPECT_EQ(cpu->m_gpr[3], 9u);
    EXPECT_EQ(cpu->m_gpr[4], 0x28001000u);
    EXPECT_EQ(cpu->m_gpr[1], 0xC000FFF8u);
    EXPECT_EQ(sch.current()->priority, 10);
}

TEST_F(SchedulerTest, CreatingOverLiveThreadFailsWithoutChangingItsContext)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    auto *thread = sch.create(*cpu, 0x2000, APP + 0x200, 7, 0, 0xC0000000);
    sch.resume(0x2000);
    cpu->m_gpr[3] = 0x2000;
    cpu->m_gpr[4] = APP + 0x400;
    cpu->m_gpr[5] = 99;
    cpu->m_gpr[6] = 0;
    cpu->m_gpr[7] = 0xC0010000;
    cpu->m_gpr[8] = 0x10000;
    cpu->m_gpr[9] = 10;
    cpu->m_gpr[10] = 4;
    Core::syscallHandler.get("OSCreateThread")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(sch.threads().size(), 2u);
    EXPECT_EQ(thread->pc, 0x200u);
    EXPECT_EQ(thread->gpr[3], 7u);
    EXPECT_EQ(thread->affinity, 2u);
    EXPECT_EQ(thread->state, Core::ThreadContext::State::Ready);
}

TEST_F(SchedulerTest, CoreQueriesFollowThreadAffinityAcrossSwitches)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    Core::syscallHandler.get("OSGetCoreId")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    Core::syscallHandler.get("OSGetMainCoreId")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    Core::syscallHandler.get("OSGetCoreCount")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 3u);
    Core::syscallHandler.get("OSIsMainCore")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);

    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.setAffinity(0x2000, 1);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.yield(*cpu));
    Core::syscallHandler.get("OSGetCoreId")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    Core::syscallHandler.get("OSIsMainCore")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    cpu->m_gpr[3] = 0x2000;
    cpu->m_gpr[4] = 4;
    Core::syscallHandler.get("OSSetThreadAffinity")(*cpu);
    EXPECT_EQ(sch.currentHandle(), 0x2000u);
    Core::syscallHandler.get("OSGetCoreId")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 2u);
    Core::syscallHandler.get("OSGetMainCoreId")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    Core::syscallHandler.get("OSGetCoreId")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
}

TEST_F(SchedulerTest, CoreIdAndUpirAgreeForEveryAffinityMask)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    EncodedInstruction instruction(0);
    instruction.rt = 4;
    instruction.ra = 1007 & 31;
    instruction.rb = 1007 >> 5;
    for (unsigned mask = 1; mask <= 7; mask++) {
        sch.setAffinity(sch.currentHandle(), mask);
        Core::syscallHandler.get("OSGetCoreId")(*cpu);
        const auto id = cpu->m_gpr[3];
        ASSERT_LT(id, 3u);
        EXPECT_NE(mask & (1u << id), 0u);
        Core::Instruction::MFSPR(*cpu, instruction);
        EXPECT_EQ(cpu->m_gpr[4], id);
        Core::syscallHandler.get("OSGetCoreId")(*cpu);
        EXPECT_EQ(cpu->m_gpr[3], id); // no migration between queries
    }
}

TEST_F(SchedulerTest, EmptyNonblockingReceiveDoesNotYieldOrTouchOutput)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto mainHandle = sch.currentHandle();
    auto *worker = sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000);
    sch.resume(0x2000);
    constexpr unsigned queue = 0x28004000, buffer = 0x28005000, output = 0x28006000;
    cpu->m_gpr[3] = queue;
    cpu->m_gpr[4] = buffer;
    cpu->m_gpr[5] = 4;
    Core::syscallHandler.get("OSInitMessageQueue")(*cpu);
    for (unsigned i = 0; i < 4; i++)
        cpu->m_memory.write<std::uint32_t>(output + i * 4, 0x12345678 + i);
    cpu->m_pc = 0x1234;
    cpu->m_lr = 0x2345;
    for (unsigned i = 0; i < 32; i++) {
        cpu->m_gpr[3] = queue;
        cpu->m_gpr[4] = output;
        cpu->m_gpr[5] = 0;
        Core::syscallHandler.get("OSReceiveMessage")(*cpu);
        ASSERT_EQ(sch.currentHandle(), mainHandle);
        EXPECT_EQ(cpu->m_gpr[3], 0u);
        EXPECT_FALSE(cpu->m_hle_redirected);
        EXPECT_EQ(cpu->m_pc, 0x1234u);
        EXPECT_EQ(cpu->m_lr, 0x2345u);
    }
    EXPECT_EQ(worker->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(worker->gpr[3], 77u);
    for (unsigned i = 0; i < 4; i++)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + i * 4), 0x12345678 + i);
}

TEST_F(SchedulerTest, BlockingReceiveRetriesAndCopiesMessageAfterProducerWakesIt)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000);
    sch.resume(0x2000);
    constexpr unsigned queue = 0x28004000, buffer = 0x28005000, output = 0x28006000, message = 0x28007000;
    cpu->m_gpr[3] = queue;
    cpu->m_gpr[4] = buffer;
    cpu->m_gpr[5] = 4;
    Core::syscallHandler.get("OSInitMessageQueue")(*cpu);
    cpu->m_pc = 0x1234;
    cpu->m_lr = 0x2345;
    cpu->m_gpr[3] = queue;
    cpu->m_gpr[4] = output;
    cpu->m_gpr[5] = 1;
    Core::syscallHandler.get("OSReceiveMessage")(*cpu);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    EXPECT_EQ(main->pc, 0x1234u);
    EXPECT_EQ(cpu->m_gpr[3], 77u);
    for (unsigned i = 0; i < 4; i++)
        cpu->m_memory.write<std::uint32_t>(message + i * 4, 0x12345678 + i);
    cpu->m_gpr[3] = queue;
    cpu->m_gpr[4] = message;
    cpu->m_gpr[5] = 0;
    Core::syscallHandler.get("OSSendMessage")(*cpu);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready);
    ASSERT_TRUE(sch.yield(*cpu));
    ASSERT_EQ(sch.currentHandle(), main->osThreadPtr);
    EXPECT_EQ(cpu->m_pc, 0x1234u);
    cpu->m_hle_redirected = false;
    Core::syscallHandler.get("OSReceiveMessage")(*cpu);
    EXPECT_FALSE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    for (unsigned i = 0; i < 4; i++)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(output + i * 4), 0x12345678 + i);
}

TEST_F(SchedulerTest, SleepWakesByVirtualClock)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);

    // Worker that we switch to while main sleeps.
    sch.create(*cpu, 0x2000u, APP + 0x200, 0u, 0u, 0xC0000000u);
    sch.resume(0x2000u);

    cpu->m_lr = 0x6000u;
    sch.sleep(*cpu, 1000); // main sleeps -> worker runs
    EXPECT_EQ(sch.currentHandle(), 0x2000u);

    // Worker exits; only the sleeping main remains, so the clock fast-forwards and main wakes.
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    EXPECT_EQ(cpu->m_pc, 0x6000u); // main resumed at its post-sleep PC
}

TEST_F(SchedulerTest, TimedWaitExpiresWhileAnotherThreadRuns)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000);
    sch.resume(0x2000);
    cpu->m_lr = 0x6000;
    ASSERT_TRUE(sch.blockWithTimeout(*cpu, 0x28004000, 100));
    EXPECT_EQ(cpu->m_gpr[3], 77u); // the wait must not clobber the producer's registers
    sch.advanceTicks(99);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    sch.advanceTicks(1);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready);
    EXPECT_FALSE(main->timedWait);
    EXPECT_EQ(main->waitKey, 0u);
    ASSERT_TRUE(sch.yield(*cpu));
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_pc, 0x6000u);
}

TEST_F(SchedulerTest, SignaledTimedWaitCancelsTimeout)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.resume(0x2000);
    cpu->m_lr = 0x6000;
    ASSERT_TRUE(sch.blockWithTimeout(*cpu, 0x28004000, 100));
    EXPECT_EQ(sch.wakeAll(0x28004000), 1u);
    sch.advanceTicks(200);
    EXPECT_EQ(main->gpr[3], 1u);
    ASSERT_TRUE(sch.yield(*cpu));
    EXPECT_EQ(cpu->m_gpr[3], 1u);
}

TEST_F(SchedulerTest, SoleTimedWaitAdvancesToItsDeadline)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    EXPECT_FALSE(sch.blockWithTimeout(*cpu, 0x28004000, 100));
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(sch.current()->state, Core::ThreadContext::State::Running);
    EXPECT_FALSE(sch.current()->timedWait);
}

TEST_F(SchedulerTest, SleepDeadlineAdvancesWithRunnableWork)
{
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.resume(0x2000);
    sch.sleep(*cpu, 100);
    sch.advanceTicks(99);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Sleeping);
    sch.advanceTicks(1);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready);
}

TEST_F(SchedulerTest, EventTimeoutUsesAlignedNanosecondsAndAutoResetWakesOne)
{
    RegisterCoreinitExtraFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    auto *waiter = sch.create(*cpu, 0x2000, APP + 0x200, 0, 0, 0xC0000000);
    sch.create(*cpu, 0x3000, APP + 0x300, 0, 0, 0xC0001000);
    sch.resume(0x2000);
    sch.resume(0x3000);
    cpu->m_gpr[3] = 0x28004000;
    cpu->m_gpr[4] = 0;
    cpu->m_gpr[5] = 1; // auto reset
    Core::syscallHandler.get("OSInitEvent")(*cpu);
    auto wait = [&]() {
        cpu->m_gpr[3] = 0x28004000;
        cpu->m_gpr[4] = 0xDEADBEEF; // ABI padding must not be part of timeout
        cpu->m_gpr[5] = 0;
        cpu->m_gpr[6] = 1000000; // 1 ms
        Core::syscallHandler.get("OSWaitEventWithTimeout")(*cpu);
    };
    wait();
    EXPECT_EQ(main->wakeTick, 62156u);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    wait();
    ASSERT_EQ(sch.currentHandle(), 0x3000u);
    cpu->m_gpr[3] = 0x28004000;
    Core::syscallHandler.get("OSSignalEvent")(*cpu);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(main->gpr[3], 1u);
    EXPECT_EQ(waiter->state, Core::ThreadContext::State::Waiting);
    sch.advanceTicks(62156);
    EXPECT_EQ(waiter->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(waiter->gpr[3], 0u);
}

TEST_F(SchedulerTest, EventSignalImmediatelyResumesHigherPriorityWaiter)
{
    RegisterCoreinitExtraFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    auto *worker = sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000, 17);
    sch.resume(0x2000);
    cpu->m_gpr[3] = 0x28004000;
    cpu->m_gpr[4] = 0;
    cpu->m_gpr[5] = 1;
    Core::syscallHandler.get("OSInitEvent")(*cpu);
    cpu->m_lr = 0x6000;
    ASSERT_TRUE(sch.blockWithTimeout(*cpu, 0x28004000, 100));
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    cpu->m_pc = 0x300;
    cpu->m_lr = 0x400;
    cpu->m_gpr[3] = 0x28004000;
    Core::syscallHandler.get("OSSignalEvent")(*cpu);
    ASSERT_EQ(sch.currentHandle(), main->osThreadPtr);
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_pc, 0x6000u);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(worker->state, Core::ThreadContext::State::Ready);
    EXPECT_EQ(worker->pc, 0x400u); // do not repeat the completed signal
    EXPECT_EQ(worker->gpr[3], 0u);
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    EXPECT_EQ(cpu->m_pc, 0x400u);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
}

TEST_F(SchedulerTest, ResumeHigherPriorityThreadPreservesCallerReturnValue)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000, 10);
    cpu->m_pc = 0x300;
    cpu->m_lr = 0x400;
    cpu->m_gpr[3] = 0x2000;
    Core::syscallHandler.get("OSResumeThread")(*cpu);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    EXPECT_EQ(cpu->m_gpr[3], 77u);
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(main->pc, 0x400u);
    EXPECT_EQ(main->gpr[3], 1u);
    ASSERT_TRUE(sch.exitCurrent(*cpu));
    EXPECT_EQ(cpu->m_pc, 0x400u);
    EXPECT_EQ(cpu->m_gpr[3], 1u);
}

TEST_F(SchedulerTest, HleRescheduleHonorsPrioritiesAndInterruptDisable)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    auto *worker = sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000, 17);
    sch.resume(0x2000);
    EXPECT_FALSE(sch.rescheduleAfterHle(*cpu));
    worker->priority = 16;
    worker->affinity = 4;
    EXPECT_FALSE(sch.rescheduleAfterHle(*cpu)); // equal-priority cross-core timeslicing is not an HLE wake
    worker->priority = 10;
    cpu->m_interruptsDisabled = true;
    EXPECT_FALSE(sch.rescheduleAfterHle(*cpu));
    EXPECT_EQ(sch.currentHandle(), main->osThreadPtr);
    cpu->m_pc = 0x300;
    cpu->m_lr = 0x400;
    cpu->m_gpr[3] = 1;
    Core::syscallHandler.get("OSRestoreInterrupts")(*cpu);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    EXPECT_EQ(main->pc, 0x400u);
    EXPECT_EQ(main->gpr[3], 0u); // prior interrupt-enabled state, not the worker's argument
    EXPECT_FALSE(main->interruptsDisabled);
    EXPECT_EQ(cpu->m_gpr[3], 77u);
}

TEST_F(SchedulerTest, MutexUnlockPreemptsToHigherPriorityWaiter)
{
    RegisterCoreinitFunctions();
    auto &sch = cpu->m_scheduler;
    sch.bootstrap(*cpu);
    const auto *main = sch.current();
    auto *owner = sch.create(*cpu, 0x2000, APP + 0x200, 77, 0, 0xC0000000, 17);
    sch.resume(0x2000);
    ASSERT_TRUE(sch.yield(*cpu));
    ASSERT_TRUE(sch.mutexTryLock(0x28004000));
    cpu->m_lr = 0x400;
    ASSERT_TRUE(sch.yield(*cpu));
    cpu->m_pc = 0x300;
    cpu->m_lr = 0x500;
    cpu->m_gpr[3] = 0x28004000;
    Core::syscallHandler.get("OSLockMutex")(*cpu);
    ASSERT_EQ(sch.currentHandle(), 0x2000u);
    EXPECT_EQ(main->state, Core::ThreadContext::State::Waiting);
    cpu->m_pc = 0x600;
    cpu->m_lr = 0x700;
    cpu->m_gpr[3] = 0x28004000;
    Core::syscallHandler.get("OSUnlockMutex")(*cpu);
    ASSERT_EQ(sch.currentHandle(), main->osThreadPtr);
    EXPECT_EQ(cpu->m_pc, 0x300u); // waiter retries its lock, owner resumes past its unlock
    EXPECT_EQ(owner->pc, 0x700u);
    EXPECT_EQ(owner->gpr[3], 0u);
    cpu->m_hle_redirected = false;
    Core::syscallHandler.get("OSLockMutex")(*cpu);
    EXPECT_FALSE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_TRUE(sch.mutexTryLock(0x28004000)); // recursively owned by the waiter now
}
