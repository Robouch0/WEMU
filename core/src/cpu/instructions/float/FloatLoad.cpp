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
    void LFS(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t exts = static_cast<std::int16_t>(instr.d);
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + exts;
        float f;

        const auto raw = cpu.m_memory.read<std::uint32_t>(ea);
        std::memcpy(&f, &raw, sizeof(f));
        cpu.m_fpr[instr.frt] = static_cast<double>(f);
    }

    /**
     * @brief Load Floating-Point Double.
     *        EA = (RA|0) + EXTS(D). FRT = MEM(EA, 8).
     *        Loads a 64-bit IEEE 754 double from memory into FRT.
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as FRT, ra, si as D).
     */
    void LFD(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t exts = static_cast<std::int16_t>(instr.d);
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + exts;

        const auto rawDouble = cpu.m_memory.read<std::uint64_t>(ea);
        std::memcpy(&cpu.m_fpr[instr.frt], &rawDouble, sizeof(double));
    }

} // namespace Core::Instruction
