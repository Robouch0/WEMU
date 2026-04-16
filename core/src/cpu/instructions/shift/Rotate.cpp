/*
** EPITECH PROJECT, 2026
** core
** File description:
** Rotate
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

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
