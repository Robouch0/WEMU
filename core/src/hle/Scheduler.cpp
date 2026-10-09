/*
** EPITECH PROJECT, 2026
** core
** File description:
** Scheduler -- cooperative thread scheduler implementation
*/

#include "Scheduler.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/memory/Memory.hpp"
#include "utils/Diagnostics.hpp"
#include "utils/Logger.hpp"

namespace Core {

    static constexpr std::uint32_t APP_CODE = Core::Memory::MemoryMap::ApplicationCode;

    void Scheduler::save(Interpreter &cpu, ThreadContext *t, const std::uint32_t resumePc)
    {
        std::memcpy(t->gpr, cpu.m_gpr, sizeof(t->gpr));
        std::memcpy(t->fpr, cpu.m_fpr, sizeof(t->fpr));
        std::memcpy(t->ps1, cpu.m_ps1, sizeof(t->ps1));
        std::memcpy(t->gqr, cpu.m_gqr, sizeof(t->gqr));
        t->cr = cpu.m_cr;
        t->xer = cpu.m_xer;
        t->fpscr = cpu.m_fpscr;
        t->lr = cpu.m_lr;
        t->ctr = cpu.m_ctr;
        t->pc = resumePc;
        t->interruptsDisabled = cpu.m_interruptsDisabled;
    }

    void Scheduler::load(Interpreter &cpu, ThreadContext *t)
    {
        std::memcpy(cpu.m_gpr, t->gpr, sizeof(t->gpr));
        std::memcpy(cpu.m_fpr, t->fpr, sizeof(t->fpr));
        std::memcpy(cpu.m_ps1, t->ps1, sizeof(t->ps1));
        std::memcpy(cpu.m_gqr, t->gqr, sizeof(t->gqr));
        cpu.m_cr = t->cr;
        cpu.m_xer = t->xer;
        cpu.m_fpscr = t->fpscr;
        cpu.m_lr = t->lr;
        cpu.m_ctr = t->ctr;
        cpu.m_pc = t->pc;
        cpu.m_nextPc = t->pc;
        cpu.m_interruptsDisabled = t->interruptsDisabled;
        cpu.m_reserveValid = false; // an lwarx/stwcx. pair must not span a context switch
        t->state = ThreadContext::State::Running;
        m_current = t;
    }

    ThreadContext *Scheduler::find(const std::uint32_t osThread)
    {
        for (auto &up: m_threads)
            if (up->osThreadPtr == osThread)
                return up.get();
        return nullptr;
    }

    std::uint32_t Scheduler::currentCoreId() const
    {
        const auto mask = m_current ? m_current->affinity & 7u : 2u;
        // Deterministic placement for the single-host-thread backend. Keep the
        // main core when allowed, otherwise select the lowest permitted core.
        return !mask || (mask & 2u) ? 1u : std::countr_zero(mask);
    }

    ThreadContext *Scheduler::pickRunnable(ThreadContext *exclude)
    {
        // Highest priority (lowest Cafe number) wins; round-robin from just past the last pick
        // breaks ties, so repeated yields cycle through the Ready threads of that priority.
        const std::size_t n = m_threads.size();
        auto scan = [&]() -> ThreadContext * {
            ThreadContext *best = nullptr;
            std::size_t bestIdx = 0;
            for (std::size_t i = 0; i < n; i++) {
                const std::size_t idx = (m_rrIndex + 1 + i) % n;
                ThreadContext *t = m_threads[idx].get();
                if (t != exclude && t->state == ThreadContext::State::Ready && (!best || t->priority < best->priority)) {
                    best = t;
                    bestIdx = idx;
                }
            }
            if (best)
                m_rrIndex = bestIdx;
            return best;
        };
        if (ThreadContext *t = scan())
            return t;

        // Nobody ready: fast-forward the clock to the earliest sleeper and wake it.
        ThreadContext *earliest = nullptr;
        for (auto &up: m_threads)
            if ((up->state == ThreadContext::State::Sleeping || (up->state == ThreadContext::State::Waiting && up->timedWait))
                && (!earliest || up->wakeTick < earliest->wakeTick))
                earliest = up.get();
        if (earliest) {
            advanceTicks(earliest->wakeTick > m_now ? earliest->wakeTick - m_now : 0);
            if (ThreadContext *t = scan())
                return t;
            if (exclude && exclude->state == ThreadContext::State::Ready)
                return exclude;
        }
        return nullptr;
    }

    void Scheduler::bootstrap(Interpreter &cpu)
    {
        if (m_current)
            return;
        // The main thread's OSThread* must point at real guest memory: titles read fields out of
        // the struct returned by OSGetCurrentThread (a synthetic out-of-map handle faults).
        constexpr std::uint32_t OS_THREAD_SIZE = 0x680;
        constexpr std::uint32_t OS_THREAD_TAG_OFFSET = 0x32C; // 'tHrD'
        m_mainHandle = cpu.m_memory.heapAllocate(OS_THREAD_SIZE, 8);
        for (std::uint32_t off = 0; off < OS_THREAD_SIZE; off += 4)
            cpu.m_memory.write<std::uint32_t>(m_mainHandle + off, 0);
        cpu.m_memory.write<std::uint32_t>(m_mainHandle + OS_THREAD_TAG_OFFSET, 0x74487244u);

        auto t = std::make_unique<ThreadContext>();
        t->state = ThreadContext::State::Running;
        t->osThreadPtr = m_mainHandle;
        t->name = "main";
        m_current = t.get();
        m_threads.push_back(std::move(t));
    }

    ThreadContext *Scheduler::create(Interpreter &cpu, const std::uint32_t osThread, const std::uint32_t entry, const std::uint32_t argc,
                                     const std::uint32_t argv, const std::uint32_t stackTop, const std::int32_t priority)
    {
        auto *existing = find(osThread);
        if (existing && existing->state != ThreadContext::State::Finished)
            return nullptr;
        auto t = std::make_unique<ThreadContext>();
        t->priority = priority;
        t->pc = entry - APP_CODE; // offset-space
        t->lr = Core::RETURN_SENTINEL - APP_CODE; // returning from the entry => thread exit
        // Cafe starts SP 8 bytes below the stack top: the EABI prologue saves LR at [sp+4], so an
        // SP at the exact top writes above the stack (MK8 places heap block headers right there).
        t->gpr[1] = (stackTop & ~0xFu) - 8;
        t->gpr[2] = cpu.m_gpr[2]; // inherit SDA bases (shared address space)
        t->gpr[13] = cpu.m_gpr[13];
        t->gpr[3] = argc;
        t->gpr[4] = argv;
        t->osThreadPtr = osThread;
        t->state = ThreadContext::State::Paused; // Cafe threads start suspended
        ThreadContext *p = t.get();
        if (existing) {
            // Reusing guest OSThread storage must replace its finished context;
            // a duplicate handle would make resume/find keep selecting the old one.
            *existing = std::move(*t);
            p = existing;
        } else {
            m_threads.push_back(std::move(t));
        }
        Utils::Log::debug("[SCHED] created thread handle=0x{:08X} entry=0x{:08X}", osThread, entry);
        return p;
    }

    void Scheduler::resume(const std::uint32_t osThread)
    {
        if (ThreadContext *t = find(osThread); t && (t->state == ThreadContext::State::Paused || t->state == ThreadContext::State::Waiting))
            t->state = ThreadContext::State::Ready;
    }

    bool Scheduler::yield(Interpreter &cpu)
    {
        ThreadContext *next = pickRunnable(m_current);
        if (!next || next == m_current)
            return false; // nobody else runnable -> keep running current

        save(cpu, m_current, cpu.m_lr);
        m_current->state = ThreadContext::State::Ready;
        load(cpu, next);
        cpu.m_hle_redirected = true;
        return true;
    }

    void Scheduler::sleep(Interpreter &cpu, const std::uint64_t ticks)
    {
        ThreadContext *prev = m_current;
        prev->wakeTick = m_now + ticks;
        prev->state = ThreadContext::State::Sleeping;

        ThreadContext *next = pickRunnable(prev);
        if (!next || next == prev) { // only this thread exists -> time simply advances, keep running
            prev->state = ThreadContext::State::Running;
            return;
        }
        save(cpu, prev, cpu.m_lr);
        load(cpu, next);
        cpu.m_hle_redirected = true;
    }

    bool Scheduler::join(Interpreter &cpu, const std::uint32_t osThread)
    {
        ThreadContext *target = find(osThread);
        if (!target || target->state == ThreadContext::State::Finished)
            return false; // already done -> return immediately

        ThreadContext *prev = m_current;
        prev->state = ThreadContext::State::Waiting;
        prev->joinTarget = osThread;

        ThreadContext *next = pickRunnable(prev);
        if (!next || next == prev) { // would deadlock; avoid hanging by just continuing
            prev->state = ThreadContext::State::Running;
            prev->joinTarget = 0;
            return false;
        }
        save(cpu, prev, cpu.m_lr);
        load(cpu, next);
        cpu.m_hle_redirected = true;
        return true;
    }

    bool Scheduler::blockOn(Interpreter &cpu, const std::uint32_t key, const bool retryInstruction)
    {
        ThreadContext *prev = m_current;
        ThreadContext *next = pickRunnable(prev);
        if (!next || next == prev)
            return false; // nobody else runnable; waiting would deadlock the host thread

        prev->state = ThreadContext::State::Waiting;
        prev->waitKey = key;
        // WEMU_WATCH_SIGNAL=<hexkey>: log the wait call-site (LR) the first few times a thread parks
        // on this key, so the predicate loop that keeps re-waiting can be located and disassembled.
        static const std::uint32_t watchWait = []() -> std::uint32_t {
            const char *e = std::getenv("WEMU_WATCH_SIGNAL");
            return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0;
        }();
        if (watchWait && key == watchWait) {
            static int n = 0;
            if (n++ < 3) {
                Utils::Log::error("[SCHED] park key=0x{:08X} thread=0x{:08X} lr=0x{:08X} -- back-chain:", key, prev->osThreadPtr,
                                  cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode);
                std::uint32_t sp = cpu.m_gpr[1];
                for (int f = 0; f < 10 && sp; f++) {
                    std::uint32_t nextSp = 0, savedLr = 0;
                    try {
                        nextSp = cpu.m_memory.read<std::uint32_t>(sp);
                        savedLr = cpu.m_memory.read<std::uint32_t>(sp + 4);
                    } catch (...) {
                        break;
                    }
                    if (savedLr)
                        Utils::Log::error("           #{} sp=0x{:08X} ret={}", f, sp, Core::Diag::symbolize(cpu, savedLr));
                    if (nextSp <= sp)
                        break;
                    sp = nextSp;
                }
            }
        }
        save(cpu, prev, retryInstruction ? cpu.m_pc : cpu.m_lr);
        load(cpu, next);
        cpu.m_hle_redirected = true;
        return true;
    }

    void Scheduler::advanceFrame(const std::uint64_t period)
    {
        // CPU execution and waits may already have accounted for this frame's time.
        const auto elapsed = m_now - m_lastFrameTick;
        advanceTicks(elapsed < period ? period - elapsed : 0);
        m_lastFrameTick = m_now;
    }

    void Scheduler::advanceTicks(const std::uint64_t ticks)
    {
        m_now += std::min(ticks, std::numeric_limits<std::uint64_t>::max() - m_now);
        for (auto &t: m_threads) {
            if (t->wakeTick > m_now)
                continue;
            if (t->state == ThreadContext::State::Sleeping)
                t->state = ThreadContext::State::Ready;
            else if (t->state == ThreadContext::State::Waiting && t->timedWait) {
                t->state = ThreadContext::State::Ready;
                t->timedWait = false;
                t->waitKey = 0;
                t->gpr[3] = 0;
            }
        }
    }

    bool Scheduler::blockWithTimeout(Interpreter &cpu, const std::uint32_t key, const std::uint64_t ticks)
    {
        if (!ticks || !m_current) {
            cpu.m_gpr[3] = 0;
            return false;
        }
        auto *prev = m_current;
        prev->state = ThreadContext::State::Waiting;
        prev->waitKey = key;
        prev->timedWait = true;
        prev->wakeTick = m_now + std::min(ticks, std::numeric_limits<std::uint64_t>::max() - m_now);
        save(cpu, prev, cpu.m_lr);
        auto *next = pickRunnable(prev);
        if (!next || next == prev) {
            prev->state = ThreadContext::State::Running;
            prev->waitKey = 0;
            prev->timedWait = false;
            cpu.m_gpr[3] = 0;
            return false;
        }
        load(cpu, next);
        cpu.m_hle_redirected = true;
        return true;
    }

    bool Scheduler::mutexLock(Interpreter &cpu, const std::uint32_t addr)
    {
        MutexState &m = m_mutexes[addr];
        const std::uint32_t self = m_current->osThreadPtr;
        if (m.owner == 0 || m.owner == self) { // free, or recursive re-entry
            m.owner = self;
            m.count++;
            return true;
        }
        // Held by another thread: park on the mutex address. The SC re-executes on wake, so the
        // acquire is retried against whoever owns the mutex then.
        if (blockOn(cpu, addr, /*retryInstruction=*/true))
            return false;
        // Nobody else runnable means the owner can never release; stealing the lock is the only
        // way to keep the single host thread alive. Loud because it usually flags an HLE gap.
        Utils::Log::debug("[SCHED] mutex 0x{:08X}: stealing from non-runnable owner 0x{:08X}", addr, m.owner);
        m.owner = self;
        m.count = 1;
        return true;
    }

    bool Scheduler::mutexTryLock(const std::uint32_t addr)
    {
        MutexState &m = m_mutexes[addr];
        const std::uint32_t self = m_current->osThreadPtr;
        if (m.owner != 0 && m.owner != self)
            return false;
        m.owner = self;
        m.count++;
        return true;
    }

    void Scheduler::mutexUnlock(const std::uint32_t addr)
    {
        auto it = m_mutexes.find(addr);
        if (it == m_mutexes.end() || it->second.owner != m_current->osThreadPtr)
            return; // unlocking a mutex we don't hold: ignore (init-less or double unlock)
        if (--it->second.count == 0) {
            m_mutexes.erase(it);
            wakeAll(addr);
        }
    }

    void Scheduler::mutexReset(const std::uint32_t addr)
    {
        m_mutexes.erase(addr);
        wakeAll(addr);
    }

    bool Scheduler::preempt(Interpreter &cpu)
    {
        return reschedule(cpu, cpu.m_pc, true);
    }

    bool Scheduler::rescheduleAfterHle(Interpreter &cpu)
    {
        if (!reschedule(cpu, cpu.m_lr, false))
            return false;
        cpu.m_hle_redirected = true;
        return true;
    }

    bool Scheduler::reschedule(Interpreter &cpu, std::uint32_t resumePc, bool timesliceOtherCores)
    {
        // Cafe semantics: threads are cooperative WITHIN a priority level (no timeslice between
        // equals — titles rely on running unpreempted until they block), but a higher-priority
        // thread becoming Ready preempts immediately. We approximate the "immediately" with this
        // periodic check.
        ThreadContext *prev = m_current;
        if (!prev || prev->state != ThreadContext::State::Running || cpu.m_interruptsDisabled)
            return false;
        // Safe to preempt when the other thread is strictly higher priority (Cafe preempts on
        // wake) or lives on a disjoint core (physically parallel on hardware, so guest code
        // cannot assume atomicity against it anyway). Same-core equals keep run-until-block.
        // Scan ALL Ready threads for the best QUALIFYING one — the globally-best Ready thread
        // may be a same-core equal (not eligible) while a disjoint-core thread further down
        // would run in parallel on hardware; testing only pickRunnable()'s answer starves it.
        const std::size_t n = m_threads.size();
        ThreadContext *next = nullptr;
        std::size_t nextIdx = 0;
        for (std::size_t i = 0; i < n; i++) {
            const std::size_t idx = (m_rrIndex + 1 + i) % n;
            ThreadContext *t = m_threads[idx].get();
            if (t == prev || t->state != ThreadContext::State::Ready)
                continue;
            const bool higherPriority = t->priority < prev->priority;
            const bool disjointCores = (t->affinity & prev->affinity) == 0;
            if (!higherPriority && !(timesliceOtherCores && disjointCores))
                continue;
            if (!next || t->priority < next->priority) {
                next = t;
                nextIdx = idx;
            }
        }
        if (!next)
            return false;
        m_rrIndex = nextIdx;
        save(cpu, prev, resumePc);
        prev->state = ThreadContext::State::Ready;
        load(cpu, next);
        return true;
    }

    void Scheduler::setPriority(const std::uint32_t osThread, const std::int32_t priority)
    {
        if (ThreadContext *t = find(osThread))
            t->priority = priority;
    }

    void Scheduler::setAffinity(const std::uint32_t osThread, const std::uint32_t mask)
    {
        if (ThreadContext *t = find(osThread); t && (mask & 7))
            t->affinity = mask & 7;
    }

    bool Scheduler::yieldRetryOnce(Interpreter &cpu)
    {
        ThreadContext *prev = m_current;
        if (prev->retryPc == cpu.m_pc) {
            // Budget: one slice per other thread, so a poll site cycles the whole round-robin
            // once before reporting failure to the guest.
            if (++prev->retryCount >= m_threads.size()) {
                prev->retryPc = 0;
                prev->retryCount = 0;
                return false;
            }
        } else {
            prev->retryPc = cpu.m_pc;
            prev->retryCount = 0;
        }
        ThreadContext *next = pickRunnable(prev);
        if (!next || next == prev)
            return false;

        save(cpu, prev, cpu.m_pc); // resume re-executes the polling SC instruction
        prev->state = ThreadContext::State::Ready;
        load(cpu, next);
        cpu.m_hle_redirected = true;
        return true;
    }

    bool Scheduler::parkCurrent(Interpreter &cpu)
    {
        ThreadContext *prev = m_current;
        ThreadContext *next = pickRunnable(prev);
        if (!next || next == prev)
            return false;
        prev->state = ThreadContext::State::Paused;
        save(cpu, prev, cpu.m_pc);
        load(cpu, next);
        return true;
    }

    std::size_t Scheduler::wakeAll(const std::uint32_t key)
    {
        std::size_t woken = 0;
        for (auto &up: m_threads)
            if (up->state == ThreadContext::State::Waiting && up->waitKey == key) {
                up->state = ThreadContext::State::Ready;
                up->waitKey = 0;
                if (up->timedWait) {
                    up->gpr[3] = 1;
                    up->timedWait = false;
                }
                woken++;
            }
        // WEMU_WATCH_SIGNAL=<hexkey>: log every wake targeting this sync-object address (and how
        // many waiters it released). Pinpoints whether a stalled thread's event is ever signaled.
        static const std::uint32_t watch = []() -> std::uint32_t {
            const char *e = std::getenv("WEMU_WATCH_SIGNAL");
            return e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0;
        }();
        if (watch && key == watch)
            Utils::Log::error("[SCHED] wake key=0x{:08X} woke {} thread(s)", key, woken);
        return woken;
    }

    std::size_t Scheduler::wakeOne(const std::uint32_t key)
    {
        ThreadContext *best = nullptr;
        for (auto &t: m_threads)
            if (t->state == ThreadContext::State::Waiting && t->waitKey == key && (!best || t->priority < best->priority))
                best = t.get();
        if (!best)
            return 0;
        best->state = ThreadContext::State::Ready;
        best->waitKey = 0;
        if (best->timedWait) {
            best->gpr[3] = 1;
            best->timedWait = false;
        }
        return 1;
    }

    bool Scheduler::exitCurrent(Interpreter &cpu)
    {
        m_current->state = ThreadContext::State::Finished;
        // Wake anyone joining this thread.
        for (auto &up: m_threads)
            if (up->state == ThreadContext::State::Waiting && up->joinTarget == m_current->osThreadPtr) {
                up->state = ThreadContext::State::Ready;
                up->joinTarget = 0;
            }
        ThreadContext *next = pickRunnable(m_current);
        if (!next || next == m_current)
            return false; // no more runnable threads -> caller stops the emulator
        load(cpu, next);
        return true;
    }

} // namespace Core
