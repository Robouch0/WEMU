/*
** EPITECH PROJECT, 2026
** core
** File description:
** Rotate
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    static std::uint32_t rotl32(std::uint32_t v, int n)
    {
        n &= 31;
        return n ? ((v << n) | (v >> (32 - n))) : v;
    }

    static std::uint32_t mask_bits(int mb, int me)
    {
        if (mb == 0 && me == 31) return 0xFFFFFFFFu;
        if (mb <= me)
            return (0xFFFFFFFFu >> mb) & (0xFFFFFFFFu << (31 - me));
        return ~mask_bits(me + 1, mb - 1);
    }


    void RLWINM(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        // SH = rb bits [15:11], MB and ME from union fields
        const std::uint32_t r = rotl32(cpu.m_gpr[i.rt], i.rb);
        const std::uint32_t m = mask_bits(i.mb, i.me);
        cpu.m_gpr[i.ra] = r & m;
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }
    void RLWIMI(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t r = rotl32(cpu.m_gpr[i.rt], i.rb);
        const std::uint32_t m = mask_bits(i.mb, i.me);
        cpu.m_gpr[i.ra] = (r & m) | (cpu.m_gpr[i.ra] & ~m);
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }
    void RLWNM(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t r = rotl32(cpu.m_gpr[i.rt], cpu.m_gpr[i.rb] & 31);
        const std::uint32_t m = mask_bits(i.mb, i.me);
        cpu.m_gpr[i.ra] = r & m;
        cpu.updateCR0(cpu.m_gprSigned[i.ra], i);
    }

    /**
     * @brief Rotate Left Word then AND with Mask (register shift).
     *        RA = ROTL(RS, RB[27:31]) & MASK(MB, ME).
     *        Shift amount comes from the low 5 bits of RB (a register).
     *        MB and ME define the mask: bits [MB:ME] are 1, all others 0.
     *        MB is at raw bits [6:10], ME is at raw bits [1:5].
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rb as shift reg, rc;
     *              MB at raw[6:10], ME at raw[1:5]).
     */
    void RLWNM(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Rotate Left Word Immediate then AND with Mask.
     *        RA = ROTL(RS, SH) & MASK(MB, ME).
     *        SH is a 5-bit immediate (inst.rb field).
     *        MB and ME define the mask: bits [MB:ME] are 1, all others 0.
     *        MB is at raw bits [6:10], ME is at raw bits [1:5].
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rb as SH, rc;
     *              MB at raw[6:10], ME at raw[1:5]).
     */
    void RLWINM(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Rotate Left Word Immediate then Mask Insert.
     *        RA = (ROTL(RS, SH) & MASK(MB, ME)) | (RA & ~MASK(MB, ME)).
     *        Inserts rotated bits of RS into RA at positions [MB:ME]; other bits of RA preserved.
     *        SH is a 5-bit immediate (inst.rb field).
     *        MB is at raw bits [6:10], ME is at raw bits [1:5].
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest/src, rb as SH, rc;
     *              MB at raw[6:10], ME at raw[1:5]).
     */
    void RLWIMI(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
