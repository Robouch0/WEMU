/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatConvert
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Floating-Point Convert to Integer Word.
     *        FRT = (int32_t)FRB, rounded per FPSCR[RN] rounding mode.
     *        The result is a 32-bit signed integer stored as a bit pattern in FRT.
     *        Updates FPSCR exception bits.
     *        If RC=1, updates CR1.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, rb as FRB, rc).
     */
    void FCTIW(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Floating-Point Convert to Integer Word with Round toward Zero.
     *        FRT = (int32_t)FRB, always truncated toward zero (ignores FPSCR[RN]).
     *        The result is a 32-bit signed integer stored as a bit pattern in FRT.
     *        Updates FPSCR exception bits.
     *        If RC=1, updates CR1.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, rb as FRB, rc).
     */
    void FCTIWZ(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
