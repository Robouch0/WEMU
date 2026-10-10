/*
** EPITECH PROJECT, 2026
** core
** File description:
** Scheduler -- cooperative (non-preemptive) thread scheduler for OSThread HLE
**
** All guest threads run on the single host thread. A thread runs until it voluntarily yields,
** sleeps, blocks (join) or exits; only then does the scheduler save its CPU context and load
** another runnable thread's context into the interpreter's live registers. This gives correct
** OSThread semantics with zero locks and fully deterministic behaviour.
*/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpu/interpreter/Registers.hpp"

namespace Core {
    class Interpreter;

    // A full snapshot of the CPU register file plus scheduling bookkeeping for one guest thread.
    struct ThreadContext {
            std::uint32_t gpr[32]{};
            double fpr[32]{};
            double ps1[32]{};
            std::uint32_t gqr[8]{};
            Core::ConditionRegister cr{};
            Core::FixedPointExceptionRegister xer{};
            Core::FloatingPointStatusAndControlRegister fpscr{};
            std::uint32_t lr{};
            std::uint32_t ctr{};
            std::uint32_t pc{}; // resume PC, in interpreter offset-space
            bool interruptsDisabled{false}; // OSDisableInterrupts / uninterruptible spinlocks are per-thread CPU state

            enum class State : std::uint8_t { Ready, Running, Sleeping, Waiting, Finished, Paused };
            State state{State::Ready};
            std::uint64_t wakeTick{0}; // when Sleeping: scheduler tick to wake at
            bool timedWait{false}; // event wait: TRUE on signal, FALSE at wakeTick
            std::uint32_t osThreadPtr{0}; // guest OSThread* handle
            std::uint32_t joinTarget{0}; // when Waiting: handle being joined
            std::uint32_t waitKey{0}; // when Waiting on a sync object (event/cond): its guest address
            std::uint32_t retryPc{0}; // last PC (offset-space) that took a yieldRetryOnce slice
            std::uint32_t retryCount{0}; // consecutive yieldRetryOnce slices taken at retryPc
            std::int32_t priority{16}; // Cafe priority: 0 = highest, 31 = lowest (default 16)
            std::uint32_t affinity{2}; // core mask (bit0-2); default core 1, where main runs
            std::string name;
    };

    class Scheduler {
        public:
            // Create the implicit "main" thread that mirrors the initial live CPU state.
            void bootstrap(Interpreter &cpu);

            // OSCreateThread: register a new (suspended) thread. Returns its context.
            ThreadContext *create(Interpreter &cpu, std::uint32_t osThread, std::uint32_t entry, std::uint32_t argc, std::uint32_t argv,
                                  std::uint32_t stackTop, std::int32_t priority = 16);
            void setPriority(std::uint32_t osThread, std::int32_t priority); // OSSetThreadPriority
            void setAffinity(std::uint32_t osThread, std::uint32_t mask); // OSSetThreadAffinity
            void resume(std::uint32_t osThread); // OSResumeThread
            bool yield(Interpreter &cpu); // OSYieldThread; true if it switched
            void sleep(Interpreter &cpu, std::uint64_t ticks); // OSSleepTicks
            bool join(Interpreter &cpu, std::uint32_t osThread); // OSJoinThread; true if it blocked/switched
            bool exitCurrent(Interpreter &cpu); // current thread terminates; true if switched to another

            // Block the current thread on a sync object (event/cond guest address). Returns true if
            // it switched to another thread; false if nobody else is runnable (caller must treat the
            // wait as satisfied to avoid deadlocking the single host thread). With retryInstruction
            // the thread resumes AT the blocking SC (re-executing it) instead of returning from it.
            bool blockOn(Interpreter &cpu, std::uint32_t key, bool retryInstruction = false);
            bool blockWithTimeout(Interpreter &cpu, std::uint32_t key, std::uint64_t ticks);
            void advanceTicks(std::uint64_t ticks);
            void advanceFrame(std::uint64_t period);

            // OSMutex / OSFastMutex / spinlock backing: recursive, owner = OSThread handle, keyed
            // by the guest address of the lock object. mutexLock returns true when the lock is
            // held on return (acquired, recursed, or stolen from a dead owner); false means the
            // thread blocked and the locking SC re-executes on wake.
            bool mutexLock(Interpreter &cpu, std::uint32_t addr);
            bool mutexTryLock(std::uint32_t addr); // never blocks; true if acquired/recursed
            void mutexUnlock(std::uint32_t addr); // ignored unless current thread owns it
            void mutexReset(std::uint32_t addr); // OSInitMutex on live memory: drop stale state

            // Timeslice preemption: called from the interpreter every N instructions. Switches to
            // the next Ready thread (resuming the current one later at the same PC). Safe at any
            // instruction boundary since the full register file is saved. Returns true on switch.
            bool preempt(Interpreter &cpu);
            // Complete an HLE operation, then run a higher-priority ready thread. Unlike
            // instruction-boundary preemption, the caller must resume at LR, not repeat the HLE.
            bool rescheduleAfterHle(Interpreter &cpu);

            // For a FAILED non-blocking poll (e.g. OSReceiveMessage with no flags): give the other
            // threads one slice and re-execute the polling SC instruction on resume, so the retried
            // call sees anything a producer posted meanwhile. At most one retry per call site in a
            // row (the second consecutive empty poll at the same PC reports failure to the guest).
            // Returns true if it switched; false if the caller should return the failure now.
            bool yieldRetryOnce(Interpreter &cpu);
            std::size_t wakeAll(std::uint32_t key); // wake every thread blocked on key; returns count woken
            std::size_t wakeOne(std::uint32_t key); // highest-priority waiter

            // Park the current (synthetic) thread: it leaves the runnable set until some HLE
            // component re-arms it by hand. Returns false if nobody else is runnable, in which
            // case the thread keeps running and the caller must give it something to do.
            bool parkCurrent(Interpreter &cpu);

            [[nodiscard]] std::uint32_t currentHandle() const { return m_current ? m_current->osThreadPtr : 0; }
            [[nodiscard]] std::uint32_t currentCoreId() const;
            [[nodiscard]] std::uint32_t mainHandle() const { return m_mainHandle; }
            [[nodiscard]] const std::vector<std::unique_ptr<ThreadContext>> &threads() const { return m_threads; }
            [[nodiscard]] const ThreadContext *current() const { return m_current; }
            [[nodiscard]] std::uint64_t now() const { return m_now; } // Espresso time-base ticks
            void setCurrentName(const std::string &name)
            {
                if (m_current)
                    m_current->name = name;
            }

        private:
            void save(Interpreter &cpu, ThreadContext *t, std::uint32_t resumePc);
            void load(Interpreter &cpu, ThreadContext *t);
            ThreadContext *find(std::uint32_t osThread);
            ThreadContext *pickRunnable(ThreadContext *exclude); // may advance the clock to wake sleepers
            bool reschedule(Interpreter &cpu, std::uint32_t resumePc, bool timesliceOtherCores);

            struct MutexState {
                    std::uint32_t owner{0}; // OSThread handle of the holder (0 = free)
                    std::uint32_t count{0}; // recursion depth
            };

            std::vector<std::unique_ptr<ThreadContext>> m_threads;
            std::unordered_map<std::uint32_t, MutexState> m_mutexes; // guest lock addr → state
            ThreadContext *m_current{nullptr};
            std::size_t m_rrIndex{0}; // round-robin scan start for pickRunnable
            std::uint64_t m_now{0};
            std::uint64_t m_lastFrameTick{0};
            std::uint32_t m_mainHandle{0xEFF00000}; // synthetic handle for the implicit main thread
    };
} // namespace Core
