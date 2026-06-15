/*
** EPITECH PROJECT, 2026
** core
** File description:
** Extend
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Count Leading Zeros Word.
     *        RA = number of leading zero bits in RS (0..32).
     *        If RS = 0, RA = 32.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rc).
     */
    void CNTLZW(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        cpu.m_gpr[i.ra] = cpu.m_gpr[i.rt] == 0 ? 32 : std::countl_zero(cpu.m_gpr[i.rt]);
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }
} // namespace Core::Instruction
