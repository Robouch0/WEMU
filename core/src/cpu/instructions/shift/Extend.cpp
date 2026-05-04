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
    void CNTLZW(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Extend Sign Halfword.
     *        RA = EXTS(RS[16:31]). Sign-extends the low 16 bits of RS to 32 bits.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rc).
     */
    void EXTSH(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Extend Sign Byte.
     *        RA = EXTS(RS[24:31]). Sign-extends the low 8 bits of RS to 32 bits.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rc).
     */
    void EXTSB(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }
    void CNTLZW(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        cpu.m_gpr[i.ra] = cpu.m_gpr[i.rt] == 0 ? 32 : std::countl_zero(cpu.m_gpr[i.rt]);
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }
    void EXTSH(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        cpu.m_gpr[i.ra] = static_cast<std::uint32_t>(static_cast<std::int16_t>(cpu.m_gpr[i.rt]));
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }
    void EXTSB(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        cpu.m_gpr[i.ra] = static_cast<std::uint32_t>(static_cast<std::int8_t>(cpu.m_gpr[i.rt]));
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }


} // namespace Core::Instruction
