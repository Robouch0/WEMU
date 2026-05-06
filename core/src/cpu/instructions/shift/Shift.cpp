/*
** EPITECH PROJECT, 2026
** core
** File description:
** Shift
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Shift Left Word.
     *        RA = RS << (RB[27:31]). Shift amount is the low 5 bits of RB.
     *        Bits shifted out are lost; vacated low bits are filled with zeros.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rb, rc).
     */
    void SLW(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t shift = cpu.m_gpr[instr.rb] & 0b111111;

        cpu.m_gpr[instr.ra] = shift >= 32 ? 0 : cpu.m_gpr[instr.rs] << shift;
        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }

    /**
     * @brief Shift Right Word.
     *        RA = RS >> (RB[27:31]). Logical (unsigned) right shift.
     *        Shift amount is the low 5 bits of RB.
     *        Vacated high bits are filled with zeros.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rb, rc).
     */
    void SRW(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t shift = cpu.m_gpr[instr.rb] & 0b111111;

        cpu.m_gpr[instr.ra] = shift >= 32 ? 0 : cpu.m_gpr[instr.rs] >> shift;
        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }

    /**
     * @brief Shift Right Algebraic Word.
     *        RA = RS >>A (RB[27:31]). Arithmetic (signed) right shift.
     *        Shift amount is the low 5 bits of RB.
     *        Vacated high bits are filled with the sign bit of RS.
     *        XER[CA] is set if RS is negative and any 1-bits are shifted out.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rb, rc).
     */
    void SRAW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint8_t n = cpu.m_gpr[instr.rb] & 0x1F;
        const std::uint32_t r = cpu.m_gpr[instr.rs] << (32 - n) | cpu.m_gpr[instr.rs] >> n;
        const std::uint32_t m = (cpu.m_gpr[instr.rb] >> 5 & 1) == 0 ? 0XFFFFFFFF >> n : 0;
        const std::uint32_t s = cpu.m_gpr[instr.rs] >> 31;

        cpu.m_gpr[instr.ra] = r & m | (s ? 0xFFFFFFFF : s) & ~m;
        cpu.m_xer.ca = s & ((r & ~m) != 0);

        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }

    /**
     * @brief Shift Right Algebraic Word Immediate.
     *        RA = RS >>A SH. Arithmetic (signed) right shift by immediate amount SH.
     *        SH is a 5-bit immediate (inst.rb field).
     *        Vacated high bits are filled with the sign bit of RS.
     *        XER[CA] is set if RS is negative and any 1-bits are shifted out.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rb as SH, rc).
     */
    void SRAWI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint8_t n = instr.sh;
        const std::uint32_t r = cpu.m_gpr[instr.rs] << (32 - n) | cpu.m_gpr[instr.rs] >> n;
        const std::uint32_t m = 0XFFFFFFFF >> n;
        const std::uint32_t s = cpu.m_gpr[instr.rs] >> 31;

        cpu.m_gpr[instr.ra] = r & m | (s ? 0xFFFFFFFF : s) & ~m;
        cpu.m_xer.ca = s & ((r & ~m) != 0);

        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }

} // namespace Core::Instruction
