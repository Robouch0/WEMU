/*
** EPITECH PROJECT, 2025
** core
** File description:
** Sub
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    //(in the PPC doc ¬ signify NOT operator)

    /**
     * @brief The sum ¬(RA) + EXTS(SI) + 1 is placed into register RT.
     * @param cpu
     * @param instr
     */
    void SUBFIC(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const int32_t imm = static_cast<int16_t>(instr.si);
        const uint64_t result = static_cast<uint64_t>(~cpu.m_gpr[instr.ra]) + static_cast<uint64_t>(static_cast<uint32_t>(imm)) + 1;

        cpu.m_gpr[instr.rt] = static_cast<uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;
    }

    /**
     * @brief The sum ¬(RA) + (RB) + 1  is placed into register RT.
     * @param cpu
     * @param instr
     */
    void SUBF(Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.rt] = ~cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb] + 1;

        cpu.updateOverflow(-cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
    }
}
