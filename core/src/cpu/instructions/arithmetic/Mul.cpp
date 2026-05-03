/*
** EPITECH PROJECT, 2025
** core
** File description:
** Mul
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void MULLI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t imm = static_cast<std::int16_t>(instr.si);
        const std::int64_t result = static_cast<std::int64_t>(cpu.m_gprSigned[instr.ra]) * static_cast<std::int64_t>(imm);

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
    }

    void MULLW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int64_t result = static_cast<std::int64_t>(cpu.m_gprSigned[instr.ra]) * static_cast<std::int64_t>(cpu.m_gprSigned[instr.rb]);
        const bool isOverflow = (result < INT32_MIN || result > INT32_MAX);

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);

        cpu.updateOverflow(isOverflow, instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    void MULHW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int64_t result = static_cast<std::int64_t>(cpu.m_gprSigned[instr.ra]) * static_cast<std::int64_t>(cpu.m_gprSigned[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result >> 32);

        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    void MULHWU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(cpu.m_gpr[instr.ra]) * static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result >> 32);

        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }
}; // namespace Core::Instruction
