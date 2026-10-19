/*
** EPITECH PROJECT, 2026
** core
** File description:
** X64Jit -- PowerPC to x86-64 block recompiler
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "cpu/types/InstructionID.hpp"

namespace Core {
    class Interpreter;
}

namespace Core::Jit {
    // Why generated code returned to the interpreter loop. Call-outs return Continue (zero) to let
    // the block go on; the other values are shared with the emitted exit stubs.
    enum class ExitReason : std::uint32_t {
        Continue = 0,
        Budget, // the next block would start at or after the service tick: m_pc = pc
        Revalidate, // the next block predates the last code write: m_pc = pc, compare its words
        LookupMiss, // no compiled block at pc (not compiled yet, hook, sentinel, ...): m_pc = pc
        Done, // the block left early (code write, HLE) or hit the spin threshold: m_pc = pc, note it
        Fatal, // a guest exception killed the thread or stopped the interpreter; m_pc already moved
        Rethrow, // a host exception must leave the interpreter
        Suffix, // the interpreter finishes the block from instruction `index`: m_pc = pc
    };

    // State shared with generated code, which keeps its address in r15. Standard layout: emitted
    // code addresses the fields with offsetof.
    struct Context {
            std::uint64_t retired{}; // kept in r13 while generated code runs
            std::uint64_t retireLimit{};
            std::uint64_t lastStoreRetired{};
            std::uint64_t spinThreshold{~std::uint64_t{0}};
            std::uint8_t *gprBase{}; // rbx: &Interpreter::m_gpr[0]
            std::uint8_t *ramBias{}; // r12: host address of guest address 0 in main RAM
            const void *slowPages{}; // r14: Memory's slow-page bitmap
            std::uint8_t *stackHost{}; // host address of Memory::STACK_BASE
            std::uint64_t scratch{}; // spill slot of slow paths
            std::uint32_t generation{1}; // compiled blocks are valid while their slot matches
            ExitReason reason{};
            std::uint32_t exitPc{};
            std::uint32_t blockPc{};
            std::uint32_t index{};
            std::uint32_t blockStores{};
    };

    struct GuestInstruction {
            std::uint32_t raw;
            InstructionID id;
    };

    // Compiles the interpreter's decoded blocks to x86-64 and runs them chained, returning to the
    // interpreter loop only for what it has to handle (service ticks, hooks, sentinels, HLE, code
    // writes, faults). Guest state stays in the Interpreter. Single emulation thread.
    class X64Jit {
        public:
            struct Exit {
                    ExitReason reason;
                    std::uint32_t pc; // memory-relative, see ExitReason
                    std::uint32_t blockPc; // Suffix: block start
                    std::uint32_t index; // Suffix: first instruction left to the interpreter
                    bool stores; // Done, Fatal and Suffix: the exited block may write memory
            };
            struct Stats {
                    std::uint64_t compiled{}, flushes{}, revalidated{}, recompiled{}, generations{}, entries{}, links{};
                    std::uint64_t instructions{}, callouts{}; // compiled, and how many of them call out
                    std::uint64_t exits[8]{};
                    std::uint64_t compileNanoseconds{}, codeBytes{};
            };

            // Null when the host lacks a required CPU feature or code memory cannot be mapped.
            // `profile` counts block executions for report().
            static std::unique_ptr<X64Jit> create(Interpreter &cpu, bool profile = false);
            ~X64Jit();
            X64Jit(const X64Jit &) = delete;
            X64Jit &operator=(const X64Jit &) = delete;

            // Spin-yield: a block without stores leaves once this many instructions retired since
            // the last block with stores.
            void setSpinThreshold(std::uint64_t instructions) noexcept;
            // Called before entering: a new code epoch starts a generation (blocks revalidate on
            // entry), a different hook count drops all code.
            void sync(std::uint64_t codeEpoch, std::size_t hookCount);
            // True when a compiled block for pc exists; it is then the target of the next run().
            bool prepare(std::uint32_t pc);
            bool compile(std::uint32_t pc, std::span<const GuestInstruction> instrs, bool stores);
            // Lets generated code reach the HLE hook at pc without leaving.
            void installHook(std::uint32_t pc);
            // Runs from the interpreter's m_pc until an exit. Rethrows parked host exceptions.
            Exit run(std::uint64_t &retired, std::uint64_t retireLimit, std::uint64_t &lastStoreRetired);
            // Revalidate exit: keeps the block if its words are unchanged, drops it otherwise.
            void revalidate(std::uint32_t pc);
            void flush();

            [[nodiscard]] const Stats &stats() const noexcept;
            // WEMU_JIT_PROFILE: logs and resets the per-interval counters.
            void report();

        private:
            struct Impl;
            explicit X64Jit(std::unique_ptr<Impl> impl);
            std::unique_ptr<Impl> m_impl;
    };
} // namespace Core::Jit
