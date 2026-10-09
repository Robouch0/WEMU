/*
** EPITECH PROJECT, 2026
** core
** File description:
** AsyncCallbacks -- deferred guest-callback pump (see header). Mirrors the AX frame-callback
** machinery: one synthetic system thread drains a FIFO of pending guest calls, each entered with
** its args in r3.. and LR set to ASYNC_CB_SENTINEL so the interpreter hands control back here when
** the callback returns.
*/

#include "AsyncCallbacks.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <string_view>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/Scheduler.hpp"
#include "utils/Logger.hpp"

namespace {

    struct PendingCall {
            std::uint32_t func{0};
            std::array<std::uint32_t, 4> args{};
    };

    std::deque<PendingCall> g_queue;
    bool g_inFlight = false; // a callback is currently executing on the pump thread

    bool traceEnabled()
    {
        static const bool enabled = [] {
            const char *e = std::getenv("WEMU_ASYNC_TRACE");
            return e && e[0] == '1';
        }();
        return enabled;
    }

    void trace(const std::string_view event, const std::uint32_t func = 0)
    {
        static std::uint32_t count = 0;
        if (traceEnabled() && count++ < 32)
            Utils::Log::error("[ASYNC] {} func=0x{:08X} queued={} inFlight={}", event, func, g_queue.size(), g_inFlight);
    }

    // The pump runs its callbacks a notch above the app threads so a login-complete handler fires
    // promptly even while the title loop is spinning; its own core lets the disjoint-affinity
    // preempt rule timeslice around it (same rationale as the AX pump).
    constexpr std::int32_t kPumpPriority = 8;
    constexpr std::uint32_t kPumpStackSize = 0x10000;

    Core::ThreadContext *g_pumpThread = nullptr;
    std::uint32_t g_pumpStackTop = 0;

    void ensurePumpThread(Core::Interpreter &cpu)
    {
        if (g_pumpThread)
            return;
        constexpr std::uint32_t OS_THREAD_SIZE = 0x680;
        constexpr std::uint32_t OS_THREAD_TAG_OFFSET = 0x32C;
        const std::uint32_t osThread = cpu.m_memory.heapAllocate(OS_THREAD_SIZE, 8);
        for (std::uint32_t off = 0; off < OS_THREAD_SIZE; off += 4)
            cpu.m_memory.write<std::uint32_t>(osThread + off, 0);
        cpu.m_memory.write<std::uint32_t>(osThread + OS_THREAD_TAG_OFFSET, 0x74487244u); // 'tHrD'
        g_pumpStackTop = cpu.m_memory.heapAllocate(kPumpStackSize, 0x10) + kPumpStackSize;
        g_pumpThread = cpu.m_scheduler.create(cpu, osThread, Core::ASYNC_CB_SENTINEL, 0, 0, g_pumpStackTop, kPumpPriority);
        g_pumpThread->name = "nnAsync";
        g_pumpThread->affinity = 4;
        // create() leaves it Paused, which doubles as our "idle/parked" state.
    }

    // Arm the (parked, non-current) pump thread with the next queued call and mark it Ready; the
    // scheduler switches to it at the next preempt point. Used from OnTick.
    void armParkedThread(Core::Interpreter &cpu)
    {
        const PendingCall call = g_queue.front();
        g_queue.pop_front();
        trace("arm", call.func);
        const std::uint32_t appCode = Core::Memory::MemoryMap::ApplicationCode;
        Core::ThreadContext *t = g_pumpThread;
        t->pc = call.func - appCode;
        t->lr = Core::ASYNC_CB_SENTINEL - appCode;
        t->gpr[1] = (g_pumpStackTop & ~0xFu) - 8;
        t->gpr[2] = cpu.m_gpr[2]; // shared SDA bases
        t->gpr[13] = cpu.m_gpr[13];
        t->gpr[3] = call.args[0];
        t->gpr[4] = call.args[1];
        t->gpr[5] = call.args[2];
        t->gpr[6] = call.args[3];
        t->state = Core::ThreadContext::State::Ready;
        g_inFlight = true;
    }

    // Redirect the CURRENT (pump) thread straight into the next queued call. Used from
    // OnSentinelReturn, where the live cpu registers belong to the pump thread.
    void chainCurrent(Core::Interpreter &cpu)
    {
        const PendingCall call = g_queue.front();
        g_queue.pop_front();
        trace("chain", call.func);
        const std::uint32_t appCode = Core::Memory::MemoryMap::ApplicationCode;
        cpu.m_pc = call.func - appCode;
        cpu.m_nextPc = cpu.m_pc;
        cpu.m_lr = Core::ASYNC_CB_SENTINEL - appCode;
        cpu.m_gpr[1] = (g_pumpStackTop & ~0xFu) - 8;
        cpu.m_gpr[3] = call.args[0];
        cpu.m_gpr[4] = call.args[1];
        cpu.m_gpr[5] = call.args[2];
        cpu.m_gpr[6] = call.args[3];
        g_inFlight = true;
    }

} // namespace

namespace Core::Async {

    void enqueue(std::uint32_t func, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3)
    {
        if (!func)
            return;
        g_queue.push_back(PendingCall{func, {a0, a1, a2, a3}});
        trace("enqueue", func);
    }

    void OnTick(Interpreter &cpu)
    {
        if (g_inFlight || g_queue.empty())
            return;
        ensurePumpThread(cpu);
        if (g_pumpThread->state != ThreadContext::State::Paused) {
            trace("not-parked");
            return; // still winding down from a previous call
        }
        armParkedThread(cpu);
    }

    void OnSentinelReturn(Interpreter &cpu)
    {
        trace("sentinel");
        g_inFlight = false;
        if (!g_queue.empty()) { // chain straight into the next queued call on this (pump) thread
            chainCurrent(cpu);
            return;
        }
        // Nothing more to deliver: park the pump thread and hand control to any other runnable thread.
        if (cpu.m_scheduler.parkCurrent(cpu))
            return;
        // Nobody else runnable (every guest thread is waiting): sleep so the guest clock advances
        // and timed waits eventually wake.
        cpu.m_scheduler.sleep(cpu, 1000);
    }

} // namespace Core::Async
