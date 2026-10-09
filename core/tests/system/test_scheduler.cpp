#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

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
    for (unsigned i = 12; i < 0xAC; i += 4)
        EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(BUF + i), i == 0x6C || i == 0x7C || i == 0x8C ? 0x3F800000u : 0u);
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
