/*
** EPITECH PROJECT, 2025
** core
** File description:
** Logic
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {
    /**
     * @brief OR RA, RS, RB — (RS) | (RB) is placed into RA.
     * Alias "mr rA, rS" when RS == RB.
     */
    void OR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rt] | cpu.m_gpr[instr.rb];

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }
} // namespace Core::Instruction
