/*
** EPITECH PROJECT, 2025
** core
** File description:
** Mul
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief The product (RA) + (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void MULHW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int64_t result = static_cast<std::int64_t>(cpu.m_gprSigned[instr.ra]) * static_cast<std::int64_t>(cpu.m_gprSigned[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result >> 32);

        if (instr.rc) {
            cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        }
    }

    /**
     * @brief The product (RA) + (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void MULHWU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(cpu.m_gpr[instr.ra]) * static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result >> 32);

        if (instr.rc) {
            cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        }
    }

};