// Interpreter::run() end to end: small PowerPC programs executed by the real
// dispatch loop, with and without the x64 JIT, and the loop's reactions to
// hooks, a branch into nowhere, timers, and a host window that pauses or closes.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <memory>
#include <vector>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/HostPresenter.hpp"
#include "gfx/Renderer.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "hle/Coreinit.hpp"
#include "hle/CoreinitExtra.hpp"

namespace {
    constexpr std::uint32_t App = Core::Memory::MemoryMap::ApplicationCode;
    constexpr std::uint32_t Result = 0x10001000; // r9 points here in every program

    // Host window that hands out one batch of commands per poll.
    class ScriptedPresenter final : public Core::Gfx::HostPresenter {
        public:
            std::deque<std::vector<Command>> batches;
            std::vector<bool> pauses;
            bool connected() const override { return true; }
            std::vector<Command> poll(std::chrono::milliseconds) override
            {
                if (batches.empty())
                    return {};
                auto next = std::move(batches.front());
                batches.pop_front();
                return next;
            }
            bool present(std::span<const std::uint8_t>, std::uint32_t, std::uint32_t, const Statistics &) override { return true; }
            void paused(bool value, const Statistics &) override { pauses.push_back(value); }
    };

    // r3 = 1 + 2 + ... + n (mod 2^32), stored at Result.
    std::vector<std::uint32_t> sumProgram(std::uint16_t nHigh, std::uint16_t nLow)
    {
        return {
                0x38600000, // li    r3, 0
                0x38800001, // li    r4, 1
                0x3CA00000u | nHigh, // lis   r5, nHigh
                0x60A50000u | nLow, // ori   r5, r5, nLow
                0x7CA903A6, // mtctr r5
                0x7C632214, // loop: add r3, r3, r4
                0x38840001, //       addi r4, r4, 1
                0x4200FFF8, //       bdnz loop
                0x90690000, // stw   r3, 0(r9)
                0x4E800020, // blr   -> RETURN_SENTINEL: the only thread ends
        };
    }

    std::uint32_t expectedSum(std::uint32_t n)
    {
        std::uint32_t sum = 0;
        for (std::uint32_t i = 1; i <= n; ++i)
            sum += i;
        return sum;
    }
} // namespace

class RunLoopTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_binary.sections.clear();
            cpu->m_binary.symbols.clear();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hooks.clear();
            cpu->m_running = true;
            cpu->m_interruptsDisabled = false;
            RegisterCoreinitFunctions();
            RegisterCoreinitExtraFunctions();
        }

        void TearDown() override
        {
            cpu->m_renderer = nullptr;
            cpu->m_hooks.clear();
            unsetenv("WEMU_X64_JIT");
            Core::Async::Reset();
        }

        // Loads `code` at the entry point and runs it as the main thread.
        void run(const std::vector<std::uint32_t> &code, std::uint32_t at = 0)
        {
            for (std::size_t i = 0; i < code.size(); ++i)
                cpu->m_memory.write<std::uint32_t>(App + at + static_cast<std::uint32_t>(i * 4), code[i]);
            cpu->m_binary.header.e_entry = App + at;
            cpu->m_gpr[9] = Result;
            cpu->m_gpr[1] = 0xC0FFFFF0;
            cpu->m_lr = Core::RETURN_SENTINEL - App;
            cpu->m_memory.write<std::uint32_t>(Result, 0);
            cpu->run();
        }
};

TEST_F(RunLoopTest, ProgramRunsToCompletionAndTheLoopStopsWhenItReturns)
{
    run(sumProgram(0, 1000));
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), 500500u);
}

TEST_F(RunLoopTest, X64JitComputesTheSameResultAsTheInterpreterAcrossServiceTicks)
{
    // 3 million iterations: many service ticks, timeslices and JIT block exits.
    constexpr std::uint32_t n = 3000000;
    run(sumProgram(n >> 16, n & 0xFFFF));
    ASSERT_FALSE(cpu->failed());
    const auto interpreted = cpu->m_memory.read<std::uint32_t>(Result);
    EXPECT_EQ(interpreted, expectedSum(n));

    cpu->reset();
    SetUp();
    setenv("WEMU_X64_JIT", "1", 1);
    run(sumProgram(n >> 16, n & 0xFFFF));
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), interpreted);
}

TEST_F(RunLoopTest, HookedAddressRunsTheHostHandlerAndReturnsToTheCaller)
{
    int calls = 0;
    cpu->m_hooks[App + 0x40] = [&calls](Core::Interpreter &target) {
        ++calls;
        target.m_gpr[3] = target.m_gpr[3] * 2 + 1;
    };
    run({
            0x7FE802A6, // mflr  r31
            0x38600014, // li    r3, 20
            0x48000039, // bl    0x40 (hooked)
            0x90690000, // stw   r3, 0(r9)
            0x7FE803A6, // mtlr  r31
            0x4E800020, // blr
    });
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), 41u);
}

TEST_F(RunLoopTest, BranchToNullStopsWithAFailureInsteadOfSpinning)
{
    run({
            0x39800000, // li    r12, 0
            0x7D8903A6, // mtctr r12
            0x4E800420, // bctr  -> address 0
    });
    EXPECT_TRUE(cpu->failed());
}

TEST_F(RunLoopTest, AlarmSetByTheTitleFiresWhileItsMainLoopRuns)
{
    constexpr std::uint32_t Alarm = 0x10002000, Flag = 0x10003000;
    cpu->m_memory.write<std::uint32_t>(Flag, 0);
    // Callback at +0x100: *Flag = 0x77.
    for (const auto &[offset, word]: std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{
                 {0x100, 0x3D401000}, // lis  r10, 0x1000
                 {0x104, 0x39600077}, // li   r11, 0x77
                 {0x108, 0x916A3000}, // stw  r11, 0x3000(r10)
                 {0x10C, 0x4E800020}, // blr
         })
        cpu->m_memory.write<std::uint32_t>(App + offset, word);
    cpu->m_gpr[3] = Alarm;
    Core::syscallHandler.get("OSCreateAlarm")(*cpu);
    cpu->m_gpr[3] = Alarm;
    cpu->m_gpr[5] = 0;
    cpu->m_gpr[6] = 1000; // ticks
    cpu->m_gpr[7] = App + 0x100;
    Core::syscallHandler.get("OSSetAlarm")(*cpu);

    // The main loop keeps storing so it is not treated as a spin-wait: 2M iterations.
    run({
            0x3CA0001E, // lis   r5, 0x1E
            0x60A58480, // ori   r5, r5, 0x8480 (2,000,000)
            0x7CA903A6, // mtctr r5
            0x90A90004, // loop: stw r5, 4(r9)
            0x4200FFFC, //       bdnz loop
            0x4E800020, // blr
    });
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Flag), 0x77u) << "the alarm callback ran on its system thread";
}

TEST_F(RunLoopTest, ClosingTheHostWindowStopsALongRunningTitle)
{
    auto presenter = std::make_unique<ScriptedPresenter>();
    presenter->batches.push_back({{Core::Gfx::HostPresenter::Action::Stop, 0}});
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;
    const auto before = cpu->m_retired;
    constexpr std::uint32_t n = 50000000; // far longer than one service tick
    run(sumProgram(n >> 16, n & 0xFFFF));
    EXPECT_FALSE(cpu->m_running);
    EXPECT_LT(cpu->m_retired - before, 1000000u) << "stopped at the first service tick, not after the loop";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), 0u) << "the title never reached its store";
}

TEST_F(RunLoopTest, PauseFromTheHostHoldsTheTitleAndResumeLetsItFinish)
{
    using Action = Core::Gfx::HostPresenter::Action;
    auto presenter = std::make_unique<ScriptedPresenter>();
    auto *host = presenter.get();
    host->batches.push_back({{Action::Pause, 0}});
    host->batches.push_back({{Action::Resume, 0}});
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;
    constexpr std::uint32_t n = 400000;
    run(sumProgram(n >> 16, n & 0xFFFF));
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), expectedSum(n)) << "the title finishes after the pause";
    EXPECT_EQ(host->pauses, (std::vector<bool>{true, false})) << "the host saw the pause and the resume";
}

namespace {
    // Main program for the threading tests: start a higher-priority worker through the hook at
    // +0x44, which preempts main at the next timeslice, then loop 2M times storing a counter and
    // store 1 when done.
    const std::vector<std::uint32_t> kMainWithWorker = {
            0x7FE802A6, // mflr  r31
            0x48000041, // bl    0x44 (hook: start the worker)
            0x3CA0001E, // lis   r5, 0x1E
            0x60A58480, // ori   r5, r5, 0x8480 (2,000,000)
            0x7CA903A6, // mtctr r5
            0x90A90008, // loop: stw r5, 8(r9)
            0x4200FFFC, //       bdnz loop
            0x38600001, // li    r3, 1
            0x90690000, // stw   r3, 0(r9)
            0x7FE803A6, // mtlr  r31
            0x4E800020, // blr
    };
} // namespace

TEST_F(RunLoopTest, WorkerThreadFinishesAndTheTitleCarriesOnUntilMainReturns)
{
    // Worker at +0x200: copy main's done flag (Result) to Result + 12, set Result + 4 = 0x5A, return.
    for (const auto &[offset, word]: std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{
                 {0x200, 0x3D401000}, // lis  r10, 0x1000
                 {0x204, 0x818A1000}, // lwz  r12, 0x1000(r10)
                 {0x208, 0x918A100C}, // stw  r12, 0x100C(r10)
                 {0x20C, 0x3960005A}, // li   r11, 0x5A
                 {0x210, 0x916A1004}, // stw  r11, 0x1004(r10)
                 {0x214, 0x4E800020}, // blr
         })
        cpu->m_memory.write<std::uint32_t>(App + offset, word);
    cpu->m_memory.write<std::uint32_t>(Result + 4, 0);
    cpu->m_memory.write<std::uint32_t>(Result + 12, 0xFFFFFFFF);
    cpu->m_hooks[App + 0x44] = [](Core::Interpreter &target) {
        // Priority 10 beats main's 16: equal priorities on one core never timeslice (Cafe rule).
        target.m_scheduler.create(target, 0x3000, App + 0x200, 0, 0, 0xC0F00000, 10);
        target.m_scheduler.resume(0x3000);
    };
    run(kMainWithWorker);
    EXPECT_FALSE(cpu->failed());
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result + 4), 0x5Au) << "the worker ran and exited";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), 1u) << "main ran to the end after it";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result + 12), 0u) << "the worker ran while main was still looping";
}

TEST_F(RunLoopTest, FaultOnAWorkerThreadKillsOnlyThatThread)
{
    // Worker at +0x200 reads unmapped address 0x100, then would store a marker it never reaches.
    for (const auto &[offset, word]: std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{
                 {0x200, 0x80600100}, // lwz  r3, 0x100(0)  -> fault
                 {0x204, 0x3D401000}, // lis  r10, 0x1000
                 {0x208, 0x916A1004}, // stw  r11, 0x1004(r10)
                 {0x20C, 0x4E800020}, // blr
         })
        cpu->m_memory.write<std::uint32_t>(App + offset, word);
    cpu->m_memory.write<std::uint32_t>(Result + 4, 0);
    cpu->m_hooks[App + 0x44] = [](Core::Interpreter &target) {
        // Priority 10 beats main's 16: equal priorities on one core never timeslice (Cafe rule).
        target.m_scheduler.create(target, 0x3000, App + 0x200, 0, 0, 0xC0F00000, 10);
        target.m_scheduler.resume(0x3000);
    };
    run(kMainWithWorker);
    EXPECT_FALSE(cpu->failed()) << "a secondary thread's fault does not end the session";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result + 4), 0u) << "the worker died at the fault";
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), 1u) << "main still completed";
}

TEST_F(RunLoopTest, StopRequestedWhilePausedEndsTheTitleWithoutResuming)
{
    using Action = Core::Gfx::HostPresenter::Action;
    auto presenter = std::make_unique<ScriptedPresenter>();
    auto *host = presenter.get();
    host->batches.push_back({{Action::Pause, 0}});
    host->batches.push_back({{Action::Stop, 0}});
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;
    constexpr std::uint32_t n = 50000000;
    run(sumProgram(n >> 16, n & 0xFFFF));
    EXPECT_FALSE(cpu->m_running);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(Result), 0u) << "the title never ran to its end";
    EXPECT_EQ(host->pauses, std::vector<bool>{true}) << "paused, then stopped: no resume notification";
}
