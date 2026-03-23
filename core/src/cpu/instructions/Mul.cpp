/*
** EPITECH PROJECT, 2025
** core
** File description:
** Mul
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void MULLW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int64_t result = static_cast<std::int64_t>(cpu.m_gprSigned[instr.ra]) * static_cast<std::int64_t>(cpu.m_gprSigned[instr.rb]);
        const bool isOverflow = (result < INT32_MIN || result > INT32_MAX);

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);

        cpu.updateOverflow(isOverflow, instr);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
    }

    void MULHW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int64_t result = static_cast<std::int64_t>(cpu.m_gprSigned[instr.ra]) * static_cast<std::int64_t>(cpu.m_gprSigned[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result >> 32);

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
    }

    void MULHWU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(cpu.m_gpr[instr.ra]) * static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result >> 32);

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
    }
};
