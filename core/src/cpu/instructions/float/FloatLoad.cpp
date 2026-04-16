/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatLoad
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Load Floating-Point Single.
     *        EA = (RA|0) + EXTS(D). FRT = MEM(EA, 4) converted to double.
     *        Loads a 32-bit IEEE 754 float from memory and stores it in FRT as double precision.
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, ra, si as D).
     */
    void LFS(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Load Floating-Point Double.
     *        EA = (RA|0) + EXTS(D). FRT = MEM(EA, 8).
     *        Loads a 64-bit IEEE 754 double from memory into FRT.
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, ra, si as D).
     */
    void LFD(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
