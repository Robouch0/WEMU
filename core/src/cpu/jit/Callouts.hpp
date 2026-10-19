/*
** EPITECH PROJECT, 2026
** core
** File description:
** Callouts -- interpreter handlers callable from generated code
*/

#pragma once

#include <cstdint>
#include <exception>

#include "cpu/types/InstructionID.hpp"

namespace Core {
    class Interpreter;
}

namespace Core::Jit {
    // Executes one instruction through its interpreter handler on behalf of generated code, with
    // runBlock's semantics: guest exceptions follow Interpreter::handleBlockException and a store
    // that touched decoded code leaves the block. Host
    // exceptions cannot unwind through generated code, so they are parked for takePendingException.
    // Returns an ExitReason: Continue, Done, Fatal or Rethrow. After HLE (sc, bctr to an import),
    // Done only when the call switched threads, stopped the interpreter, wrote decoded code or
    // changed the hooks.
    using Callout = std::uint32_t (*)(Interpreter &cpu, std::uint32_t raw, std::uint32_t pc) noexcept;

    [[nodiscard]] Callout callout(InstructionID id) noexcept;
    [[nodiscard]] std::exception_ptr takePendingException() noexcept;

    // Runs the HLE hook at `pc` (memory-relative) as Interpreter::run does, then m_pc = LR.
    // Returns Continue, LookupMiss when the dispatch loop must take over at m_pc, or Rethrow.
    std::uint32_t runHook(Interpreter &cpu, std::uint32_t pc) noexcept;
} // namespace Core::Jit
