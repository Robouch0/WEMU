/*
** EPITECH PROJECT, 2025
** core
** File description:
** Store
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Store Word.
     *        EA = (RA|0) + EXTS(D). MEM(EA, 4) = RS.
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, si as D).
     */
    void STW(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t exts = static_cast<std::int16_t>(instr.d);
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + static_cast<std::uint32_t>(exts);

        cpu.m_memory.write<std::uint32_t>(ea, cpu.m_gpr[instr.rs]);
    }

    /**
     * @brief Store Word with Update.
     *        EA = RA + EXTS(D). MEM(EA, 4) = RS. RA = EA.
     *        RA must not be 0.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, si as D).
     */
    void STWU(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Word Indexed.
     *        EA = (RA|0) + RB. MEM(EA, 4) = RS.
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, rb).
     */
    void STWX(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Multiple Word.
     *        EA = (RA|0) + EXTS(D).
     *        Stores registers RS through r31 into consecutive words in memory.
     *        If RA is in the range of registers being stored, the result is undefined.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, si as D).
     */
    void STMW(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Halfword Indexed.
     *        EA = (RA|0) + RB. MEM(EA, 2) = RS[16:31].
     *        Stores the low-order 16 bits of RS.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, rb).
     */
    void STHX(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Byte.
     *        EA = (RA|0) + EXTS(D). MEM(EA, 1) = RS[24:31].
     *        Stores the low-order byte of RS.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, si as D).
     */
    void STB(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Halfword.
     *        EA = (RA|0) + EXTS(D). MEM(EA, 2) = RS[16:31].
     *        Stores the low-order 16 bits of RS.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, si as D).
     */
    void STH(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Store Byte Indexed.
     *        EA = (RA|0) + RB. MEM(EA, 1) = RS[24:31].
     *        Stores the low-order byte of RS.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, rb).
     */
    void STBX(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
