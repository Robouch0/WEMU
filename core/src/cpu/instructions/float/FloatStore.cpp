/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatStore
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Store Floating-Point Single.
     *        EA = (RA|0) + EXTS(D). MEM(EA, 4) = FRS converted to 32-bit IEEE 754 float.
     *        Stores FRS as a single-precision float (double → float narrowing).
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRS, ra, si as D).
     */
    void STFS(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Floating-Point Double.
     *        EA = (RA|0) + EXTS(D). MEM(EA, 8) = FRS as 64-bit IEEE 754 double.
     *        Stores FRS promoted to double precision into 8 bytes at EA.
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRS, ra, si as D).
     */
    void STFD(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
