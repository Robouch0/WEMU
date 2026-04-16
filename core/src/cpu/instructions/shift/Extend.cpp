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

} // namespace Core::Instruction
