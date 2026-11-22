/*
** EPITECH PROJECT, 2026
** core
** File description:
** Callouts -- interpreter handlers callable from generated code
*/

#include "Callouts.hpp"

#include <utility>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/jit/X64Jit.hpp"
#include "cpu/types/Instruction.hpp"
#include "cpu/types/InstructionTraits.hpp"
#include "utils/FrameProfiler.hpp"

namespace Core::Jit {
    namespace {
        std::exception_ptr g_pending; // single emulation thread

        using Handler = void (*)(Interpreter &, const EncodedInstruction &);

        // One instantiation per handler: generated code calls it directly, so each call site keeps
        // its own branch prediction instead of sharing one indirect call.
        template<Handler Fn, InstructionID Id>
        std::uint32_t invoke(Interpreter &cpu, const std::uint32_t raw, const std::uint32_t pc) noexcept
        {
            // sc and bctr (to an import sentinel) dispatch HLE. Generated code goes on after them
            // unless the call switched threads, stopped the interpreter, wrote decoded code or
            // changed the hooks: the dispatch loop handles those.
            constexpr bool hle = Id == E_SC || Id == E_BCTR;
            const std::uint64_t epoch = hle ? cpu.m_memory.codeEpoch() : 0;
            const std::size_t hooks = hle ? cpu.m_hooks.size() : 0;
            cpu.m_pc = pc;
            cpu.m_nextPc = pc + INSTR_SIZE;
            try {
                Fn(cpu, EncodedInstruction(raw));
                if constexpr (isStoreInstruction(Id)) {
                    if (cpu.m_memory.codeWritten()) {
                        cpu.m_memory.clearCodeWritten();
                        return static_cast<std::uint32_t>(ExitReason::Done);
                    }
                }
            } catch (const Core::Exception &e) {
                if (cpu.handleBlockException(e))
                    return static_cast<std::uint32_t>(ExitReason::Fatal);
            } catch (...) {
                g_pending = std::current_exception();
                return static_cast<std::uint32_t>(ExitReason::Rethrow);
            }
            if (hle && (cpu.m_hle_redirected || !cpu.m_running || cpu.m_memory.codeEpoch() != epoch || cpu.m_hooks.size() != hooks))
                return static_cast<std::uint32_t>(ExitReason::Done);
            return static_cast<std::uint32_t>(ExitReason::Continue);
        }

        constexpr Callout kCallouts[] = {
#define INSTR(name, ...) &invoke<&Core::Instruction::name, E_##name>,
#include "cpu/tables/cpu_instructions.anh"
#undef INSTR
        };
    } // namespace

    Callout callout(const InstructionID id) noexcept { return kCallouts[id]; }

    std::exception_ptr takePendingException() noexcept { return std::exchange(g_pending, nullptr); }

    std::uint32_t runHook(Interpreter &cpu, const std::uint32_t pc) noexcept
    {
        // Interpreter::run's hook dispatch: the hook replaces the function, which returns to LR.
        const std::uint64_t epoch = cpu.m_memory.codeEpoch();
        const std::size_t hooks = cpu.m_hooks.size();
        cpu.m_pc = pc;
        try {
            Profile::Scope hle(Profile::Stage::Hle);
            cpu.m_hooks.find(pc + Memory::ApplicationCode)->second(cpu);
        } catch (...) {
            g_pending = std::current_exception();
            return static_cast<std::uint32_t>(ExitReason::Rethrow);
        }
        cpu.m_pc = cpu.m_lr;
        if (!cpu.m_running || cpu.m_memory.codeEpoch() != epoch || cpu.m_hooks.size() != hooks)
            return static_cast<std::uint32_t>(ExitReason::LookupMiss);
        return static_cast<std::uint32_t>(ExitReason::Continue);
    }
} // namespace Core::Jit
