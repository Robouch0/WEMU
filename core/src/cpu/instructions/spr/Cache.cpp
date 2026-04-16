/*
** EPITECH PROJECT, 2026
** core
** File description:
** Cache
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Synchronize.
     *        Ensures all previous memory accesses are complete before subsequent ones begin.
     *        No-op in a single-threaded interpreter.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (no significant fields).
     */
    void SYNC(Interpreter &cpu, const EncodedInstruction &instr) {} // TODO

    void ISYNC(Interpreter &cpu, const EncodedInstruction &instr) {} // TODO

    /**
     * @brief Enforce In-Order Execution of I/O.
     *        Memory barrier for I/O operations.
     *        No-op in a single-threaded interpreter.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (no significant fields).
     */
    void EIEIO(Interpreter &cpu, const EncodedInstruction &instr) {} // TODOF

    /**
     * @brief Data Cache Block Store.
     *        EA = (RA|0) + RB. Initiates a store of the cache block containing EA.
     *        No-op in a single-threaded interpreter (no cache model).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: ra, rb).
     */
    void DCBST(Interpreter &cpu, const EncodedInstruction &instr) {} // TODO

    /**
     * @brief Data Cache Block Flush.
     *        EA = (RA|0) + RB. Flushes the cache block containing EA to memory.
     *        No-op in a single-threaded interpreter (no cache model).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: ra, rb).
     */
    void DCBF(Interpreter &cpu, const EncodedInstruction &instr) {} // TODO

    /**
     * @brief Data Cache Block Touch.
     *        EA = (RA|0) + RB. Hint to prefetch the cache block containing EA.
     *        No-op in a single-threaded interpreter (no cache model).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: ra, rb).
     */
    void DCBT(Interpreter &cpu, const EncodedInstruction &instr) {} // TODO

    /**
     * @brief Instruction Cache Block Invalidate.
     *        EA = (RA|0) + RB. Invalidates the instruction cache block containing EA.
     *        No-op in a single-threaded interpreter (no cache model).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: ra, rb).
     */
    void ICBI(Interpreter &cpu, const EncodedInstruction &instr) {} // TODO

} // namespace Core::Instruction
