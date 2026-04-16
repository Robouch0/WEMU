/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatCompare
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Floating-Point Compare Unordered.
     *        Compares FRA and FRB and records the result in CR field BF.
     *        BF is a 3-bit field at raw bits [23:25] selecting CR0–CR7.
     *        Sets CR[BF].lt if FRA < FRB, CR[BF].gt if FRA > FRB,
     *        CR[BF].eq if FRA == FRB, CR[BF].so if either operand is NaN.
     *        Updates FPSCR[FPCC].
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: ra as FRA, rb as FRB; BF at raw[23:25]).
     */
    void FCMPU(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
