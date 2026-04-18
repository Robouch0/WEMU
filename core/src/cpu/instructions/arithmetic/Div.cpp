/*
** EPITECH PROJECT, 2025
** core
** File description:
** Div
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief The quotient (RA) / (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void DIVW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t ra = cpu.m_gprSigned[instr.ra];
        const std::int32_t rb = cpu.m_gprSigned[instr.rb];
        const bool isOverflow = (ra == INT32_MIN && rb == -1) || (rb == 0);

        cpu.updateOverflow(isOverflow, instr);

        if (!isOverflow)
            cpu.m_gprSigned[instr.rt] = ra / rb;

        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The quotient (RA) / (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void DIVWU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ra = cpu.m_gpr[instr.ra];
        const std::uint32_t rb = cpu.m_gpr[instr.rb];
        const bool isOverflow = rb == 0;

        cpu.updateOverflow(isOverflow, instr);

        if (!isOverflow)
            cpu.m_gpr[instr.rt] = ra / rb;

        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }
};
