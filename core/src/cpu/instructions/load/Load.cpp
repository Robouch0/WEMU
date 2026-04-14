/*
** EPITECH PROJECT, 2026
** core
** File description:
** Load
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Load Word and Zero.
     *        EA = (RA|0) + EXTS(D). RT = MEM(EA, 4).
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, si as D).
     */
    void LWZ(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t exts = static_cast<std::int16_t>(instr.d);
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + static_cast<std::uint32_t>(exts);

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint32_t>(ea);
    }

    /**
     * @brief Load Word and Zero with Update.
     *        EA = RA + EXTS(D). RT = MEM(EA, 4). RA = EA.
     *        RA must not be 0 and must not equal RT.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, si as D).
     */
    void LWZU(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t exts = static_cast<std::int16_t>(instr.d);
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + static_cast<std::uint32_t>(exts);

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint32_t>(ea);
        cpu.m_gpr[instr.ra] = ea;
    }

    /**
     * @brief Load Word and Zero Indexed.
     *        EA = (RA|0) + RB. RT = MEM(EA, 4).
     *        If RA = 0, base address is 0 (not r0).
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb).
     */
    void LWZX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint32_t>(ea);
    }

    /**
     * @brief Load Halfword Algebraic.
     *        EA = (RA|0) + EXTS(D). RT = EXTS(MEM(EA, 2)).
     *        Sign-extends the loaded 16-bit value to 32 bits.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, si as D).
     */
    void LHA(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + static_cast<std::int16_t>(instr.d);

        cpu.m_gpr[instr.rt] = static_cast<std::int16_t>(cpu.m_memory.read<std::uint16_t>(ea));
    }

    /**
     * @brief Load Halfword Algebraic Indexed.
     *        EA = (RA|0) + RB. RT = EXTS(MEM(EA, 2)).
     *        Sign-extends the loaded 16-bit value to 32 bits.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb).
     */
    void LHAX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = static_cast<std::int16_t>(cpu.m_memory.read<std::uint16_t>(ea));
    }

    /**
     * @brief Load Multiple Word.
     *        EA = (RA|0) + EXTS(D).
     *        Loads consecutive words from memory into registers RT through r31.
     *        If RA is in the range of registers being loaded, the result is undefined.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, si as D).
     */
    void LMW(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Load Halfword and Zero Indexed.
     *        EA = (RA|0) + RB. RT = 0x0000 || MEM(EA, 2).
     *        Zero-extends the loaded 16-bit value to 32 bits.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb).
     */
    void LHZX(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Load Byte and Zero.
     *        EA = (RA|0) + EXTS(D). RT = 0x000000 || MEM(EA, 1).
     *        Zero-extends the loaded byte to 32 bits.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, si as D).
     */
    void LBZ(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Load Halfword and Zero.
     *        EA = (RA|0) + EXTS(D). RT = 0x0000 || MEM(EA, 2).
     *        Zero-extends the loaded 16-bit value to 32 bits.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, si as D).
     */
    void LHZ(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

    /**
     * @brief Load Byte and Zero Indexed.
     *        EA = (RA|0) + RB. RT = 0x000000 || MEM(EA, 1).
     *        Zero-extends the loaded byte to 32 bits.
     * @param cpu  Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb).
     */
    void LBZX(Interpreter &cpu, const EncodedInstruction &instr);
    // {
    // }

} // namespace Core::Instruction
