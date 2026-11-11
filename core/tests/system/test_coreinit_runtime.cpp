// coreinit calls that drive a title's run loop: interrupts, sleeping, thread
// exit, the wut entry point, ProcUI shutdown, system information and OSReport.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "gfx/HostPresenter.hpp"
#include "gfx/Renderer.hpp"
#include "hle/Coreinit.hpp"

namespace {
    constexpr std::uint32_t App = Core::Memory::MemoryMap::ApplicationCode;
    constexpr std::uint32_t Worker = 0x2000;

    // A host window that answers the commands the test queues.
    class ScriptedPresenter final : public Core::Gfx::HostPresenter {
        public:
            std::vector<Command> commands;
            bool connected() const override { return true; }
            std::vector<Command> poll(std::chrono::milliseconds) override { return std::exchange(commands, {}); }
            bool present(std::span<const std::uint8_t>, std::uint32_t, std::uint32_t, const Statistics &) override { return true; }
            void paused(bool, const Statistics &) override {}
    };
} // namespace

class CoreinitRuntimeTest : public InstructionTest {
    protected:
        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_scheduler = Core::Scheduler{};
            cpu->m_hle_redirected = false;
            cpu->m_interruptsDisabled = false;
            cpu->m_running = true;
            RegisterCoreinitFunctions();
        }

        void TearDown() override { cpu->m_renderer = nullptr; }

        static std::uint32_t call(Core::Interpreter &target, const char *name, std::uint32_t r3 = 0, std::uint32_t r4 = 0)
        {
            target.m_gpr[3] = r3;
            target.m_gpr[4] = r4;
            Core::syscallHandler.get(name)(target);
            return target.m_gpr[3];
        }

        static std::uint32_t call(const char *name, std::uint32_t r3 = 0, std::uint32_t r4 = 0) { return call(*cpu, name, r3, r4); }

        void startWorker()
        {
            cpu->m_scheduler.bootstrap(*cpu);
            cpu->m_scheduler.create(*cpu, Worker, App + 0x200, 0, 0, 0xC0000000);
            cpu->m_scheduler.resume(Worker);
        }
};

TEST_F(CoreinitRuntimeTest, InterruptDisableAndRestoreNestLikeTheCafeApi)
{
    cpu->m_scheduler.bootstrap(*cpu);
    // OSDisableInterrupts returns the previous state (1 = enabled); OSRestoreInterrupts puts it back.
    const auto outer = call("OSDisableInterrupts");
    EXPECT_EQ(outer, 1u);
    EXPECT_TRUE(cpu->m_interruptsDisabled);
    const auto inner = call("OSDisableInterrupts");
    EXPECT_EQ(inner, 0u) << "already disabled";
    EXPECT_EQ(call("OSRestoreInterrupts", inner), 0u);
    EXPECT_TRUE(cpu->m_interruptsDisabled) << "restoring the inner state keeps the outer section closed";
    EXPECT_EQ(call("OSRestoreInterrupts", outer), 0u);
    EXPECT_FALSE(cpu->m_interruptsDisabled);
    EXPECT_EQ(call("OSRestoreInterrupts", 1), 1u) << "restore reports the state it replaced";
}

TEST_F(CoreinitRuntimeTest, SleepTicksUsesTheFull64BitDurationAndLetsOtherThreadsRun)
{
    startWorker();
    const auto *main = cpu->m_scheduler.current();
    const auto start = cpu->m_scheduler.now();
    call("OSSleepTicks", 0x1, 0x00000005); // 2^32 + 5 ticks, split across r3:r4
    EXPECT_EQ(main->state, Core::ThreadContext::State::Sleeping);
    EXPECT_EQ(main->wakeTick, start + 0x100000005ull);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), Worker);
    EXPECT_TRUE(cpu->m_hle_redirected);
}

TEST_F(CoreinitRuntimeTest, SleepingAloneKeepsTheOnlyThreadRunning)
{
    cpu->m_scheduler.bootstrap(*cpu);
    const auto main = cpu->m_scheduler.currentHandle();
    call("OSSleepTicks", 0, 1000);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), main);
    EXPECT_EQ(cpu->m_scheduler.current()->state, Core::ThreadContext::State::Running);
    EXPECT_FALSE(cpu->m_hle_redirected);
}

TEST_F(CoreinitRuntimeTest, ExitingTheLastThreadStopsTheTitle)
{
    startWorker();
    call("OSExitThread", 0);
    EXPECT_TRUE(cpu->m_running) << "another thread can still run";
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), Worker);

    cpu->m_hle_redirected = false;
    call("OSExitThread", 0);
    EXPECT_FALSE(cpu->m_running);
}

TEST_F(CoreinitRuntimeTest, WutEntryJumpsToTheTitlesMainOrReportsItMissing)
{
    Core::Symbol main{};
    main.name = "main";
    main.raw.header.st_value = App + 0x1F0;
    Core::Symbol stray{};
    stray.name = "main";
    stray.raw.header.st_value = 0x10000040; // a data symbol called main is not an entry point
    cpu->m_binary.symbols = {stray, main};
    cpu->m_nextPc = 0;
    call("__init_wut");
    EXPECT_TRUE(cpu->m_hle_redirected);
    EXPECT_EQ(cpu->m_nextPc, 0x1F0u) << "PCs are relative to the code base";

    cpu->m_binary.symbols = {stray};
    cpu->m_hle_redirected = false;
    testing::internal::CaptureStderr();
    EXPECT_EQ(call("__init_wut", 0x55), 0u);
    EXPECT_NE(testing::internal::GetCapturedStderr().find("main() not found"), std::string::npos);
    EXPECT_FALSE(cpu->m_hle_redirected);
    cpu->m_binary.symbols.clear();
}

TEST_F(CoreinitRuntimeTest, ClosingTheHostWindowStopsTheTitleThroughProcUI)
{
    cpu->m_scheduler.bootstrap(*cpu);
    auto presenter = std::make_unique<ScriptedPresenter>();
    auto *host = presenter.get();
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;

    EXPECT_EQ(call("ProcUIIsRunning"), 1u);
    EXPECT_EQ(call("ProcUIProcessMessages"), 0u) << "PROCUI_STATUS_IN_FOREGROUND";
    EXPECT_TRUE(cpu->m_running);

    host->commands = {{Core::Gfx::HostPresenter::Action::Stop, 0}};
    // A host close is not a Cafe shutdown: the title stays in the foreground and is stopped directly.
    EXPECT_EQ(call("ProcUIProcessMessages"), 0u);
    EXPECT_FALSE(cpu->m_running);
    EXPECT_EQ(call("ProcUIIsRunning"), 0u);
}

TEST_F(CoreinitRuntimeTest, WithoutAHostWindowProcUIKeepsRunning)
{
    cpu->m_scheduler.bootstrap(*cpu);
    EXPECT_EQ(call("ProcUIIsRunning"), 1u);
    call("ProcUIProcessMessages");
    EXPECT_TRUE(cpu->m_running);
}

TEST_F(CoreinitRuntimeTest, SystemInfoDescribesTheEspressoClocksInEachGuestMemory)
{
    const auto check = [](Core::Interpreter &target) {
        const auto info = call(target, "OSGetSystemInfo");
        ASSERT_NE(info, 0u);
        const auto bus = target.m_memory.read<std::uint32_t>(info);
        const auto core = target.m_memory.read<std::uint32_t>(info + 4);
        EXPECT_EQ(bus, 248625000u);
        EXPECT_EQ(core, bus * 5) << "Espresso runs at five times the bus clock";
        EXPECT_EQ(target.m_memory.read<std::uint32_t>(info + 0x1C), 5u);
        EXPECT_EQ(target.m_memory.read<std::uint32_t>(info + 0x14), 0x200000u) << "core 1 has the 2 MiB L2";
        EXPECT_EQ(call(target, "OSGetSystemInfo"), info) << "the same struct on every call";
    };
    check(*cpu);
    // A second emulator instance in the same process gets its own struct in its own memory.
    auto other = std::make_unique<Core::Interpreter>(Core::Binary{});
    check(*other);
    other.reset();
    check(*cpu);
    cpu->reset(); // a reset guest memory gets a fresh struct too
    check(*cpu);
}

TEST_F(CoreinitRuntimeTest, OSReportAndOSFatalReachTheHostLog)
{
    constexpr std::uint32_t Text = 0x28200000;
    const std::string message = "level 3 loaded";
    for (std::size_t i = 0; i <= message.size(); ++i)
        cpu->m_memory.write<std::uint8_t>(Text + static_cast<std::uint32_t>(i), i < message.size() ? static_cast<std::uint8_t>(message[i]) : 0);

    testing::internal::CaptureStderr();
    call("OSReport", Text);
    call("OSFatal", Text);
    call("OSFatal", 0x00000010); // unmapped: still logged, without reading it
    call("OSReport", 0x00000010);
    const auto log = testing::internal::GetCapturedStderr();
    EXPECT_NE(log.find("[OSReport] level 3 loaded"), std::string::npos) << log;
    EXPECT_NE(log.find("[OSFatal] level 3 loaded"), std::string::npos) << log;
    EXPECT_NE(log.find("[OSFatal] <unmapped>"), std::string::npos) << log;
}

TEST_F(CoreinitRuntimeTest, DynamicLoadHandsOutAUsableModuleHandle)
{
    constexpr std::uint32_t Handle = 0x28210000;
    // Titles dereference the handle they get back, so it must be written and non-zero.
    EXPECT_EQ(call("OSDynLoad_Acquire", 0x28210100, Handle), 0u);
    EXPECT_NE(cpu->m_memory.read<std::uint32_t>(Handle), 0u);
    EXPECT_EQ(call("OSDynLoad_Acquire", 0x28210100, 0), 0u) << "a null output pointer is tolerated";
}

TEST_F(CoreinitRuntimeTest, RaisingAReadyThreadAboveTheCallerHandsItTheCpu)
{
    cpu->m_scheduler.bootstrap(*cpu);
    const auto main = cpu->m_scheduler.currentHandle();
    cpu->m_scheduler.create(*cpu, Worker, App + 0x200, 0, 0, 0xC0000000, 20); // lower priority than main (16)
    cpu->m_scheduler.resume(Worker);
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), main);

    EXPECT_EQ(call("OSSetThreadPriority", Worker, 24), 1u);
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), main) << "still below the caller";
    EXPECT_EQ(call("OSGetThreadPriority", Worker), 24u);

    call("OSSetThreadPriority", Worker, 10); // 0 is the highest Cafe priority
    EXPECT_EQ(cpu->m_scheduler.currentHandle(), Worker) << "a higher-priority ready thread preempts at once";
    EXPECT_TRUE(cpu->m_hle_redirected);
}

TEST_F(CoreinitRuntimeTest, FastMutexUnlockWakesTheThreadWaitingForIt)
{
    startWorker();
    const auto *main = cpu->m_scheduler.current();
    constexpr std::uint32_t Mutex = 0x28220000;
    call("OSFastMutex_Init", Mutex);
    call("OSFastMutex_Lock", Mutex);
    ASSERT_TRUE(cpu->m_scheduler.yield(*cpu));
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), Worker);
    auto *worker = cpu->m_scheduler.current();

    EXPECT_EQ(call("OSFastMutex_TryLock", Mutex), 0u) << "held by main";
    call("OSFastMutex_Lock", Mutex);
    EXPECT_EQ(worker->state, Core::ThreadContext::State::Waiting);
    ASSERT_EQ(cpu->m_scheduler.currentHandle(), main->osThreadPtr);

    cpu->m_hle_redirected = false;
    call("OSFastMutex_Unlock", Mutex);
    EXPECT_EQ(worker->state, Core::ThreadContext::State::Ready) << "the waiter can retry its lock";
    EXPECT_EQ(call("OSFastMutex_TryLock", Mutex), 1u) << "and the mutex is free again";
}

TEST_F(CoreinitRuntimeTest, OnlyEnabledScreensAreSentToTheHostWhenFlipped)
{
    class CountingPresenter final : public Core::Gfx::HostPresenter {
        public:
            std::vector<std::pair<std::uint32_t, std::uint32_t>> frames;
            std::uint32_t firstPixel = 0;
            bool connected() const override { return true; }
            std::vector<Command> poll(std::chrono::milliseconds) override { return {}; }
            bool present(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, const Statistics &) override
            {
                frames.emplace_back(width, height);
                std::memcpy(&firstPixel, rgba.data(), 4);
                return true;
            }
            void paused(bool, const Statistics &) override {}
    };
    setenv("WEMU_UNTHROTTLED", "1", 1);
    auto presenter = std::make_unique<CountingPresenter>();
    auto *host = presenter.get();
    Renderer renderer(std::move(presenter));
    cpu->m_renderer = &renderer;
    constexpr std::uint32_t Tv = 0x29000000;
    call("OSScreenInit");
    call("OSScreenSetBufferEx", 0, Tv);
    call("OSScreenClearBufferEx", 0, 0x10203000);

    call("OSScreenFlipBuffersEx", 0);
    EXPECT_TRUE(host->frames.empty()) << "a disabled screen is never presented";

    call("OSScreenEnableEx", 0, 1);
    call("OSScreenClearBufferEx", 0, 0x40506000); // the buffer drawn after the first flip
    call("OSScreenFlipBuffersEx", 0);
    ASSERT_EQ(host->frames.size(), 1u);
    EXPECT_EQ(host->frames[0], std::make_pair(1280u, 720u));
    EXPECT_EQ(host->firstPixel & 0x00FFFFFFu, 0x605040u) << "the flipped buffer is the one just drawn";

    call("OSScreenEnableEx", 0, 0);
    call("OSScreenFlipBuffersEx", 0);
    EXPECT_EQ(host->frames.size(), 1u);
    unsetenv("WEMU_UNTHROTTLED");
}
