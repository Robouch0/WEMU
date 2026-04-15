/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatMove
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Floating-Point Move Register.
     *        FRT = FRB. Copies FRB into FRT without modification.
     *        If RC=1, updates CR1.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, rb as FRB, rc).
     */
    void FMR(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Floating-Point Negate.
     *        FRT = -FRB. Flips the sign bit of FRB and stores in FRT.
     *        Does not alter the magnitude or NaN payload.
     *        If RC=1, updates CR1.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, rb as FRB, rc).
     */
    void FNEG(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Floating-Point Absolute Value.
     *        FRT = |FRB|. Forces the sign bit of FRB to 0 and stores in FRT.
     *        Does not alter the magnitude or NaN payload.
     *        If RC=1, updates CR1.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, rb as FRB, rc).
     */
    void FABS(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Floating-Point Round to Single Precision.
     *        FRT = (float)FRB. Rounds the double-precision value in FRB to
     *        single-precision and stores the result (as double) in FRT.
     *        Updates FPSCR exception bits.
     *        If RC=1, updates CR1.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, rb as FRB, rc).
     */
    void FRSP(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
