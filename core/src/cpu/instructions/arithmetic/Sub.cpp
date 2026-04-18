/*
** EPITECH PROJECT, 2025
** core
** File description:
** Sub
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief The sum ¬(RA) + EXTS(SI) + 1 is placed into register RT.
     * @param cpu
     * @param instr
     */
    void SUBFIC(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t imm = static_cast<std::int16_t>(instr.si);
        const std::uint64_t result = static_cast<std::uint64_t>(~cpu.m_gpr[instr.ra]) + static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm)) + 1;

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;
    }

    /**
     * @brief The sum ¬(RA) + (RB) + 1  is placed into register RT.
     * @param cpu
     * @param instr
     */
    void SUBF(Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.rt] = ~cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb] + 1;

        cpu.updateOverflow(-cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The sum ¬(RA) + (RB) + 1 is placed into register RT. CA is set.
     * @param cpu
     * @param instr
     */
    void SUBFC(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(~cpu.m_gpr[instr.ra]) + static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]) + 1;

        cpu.m_gpr[instr.rt] = static_cast<uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;

        cpu.updateOverflow(-cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    /**
    * @brief The sum ¬(RA) + (RB) + CA is placed into register RT. CA is set.
    * @param cpu
    * @param instr
    */
    void SUBFE(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(~cpu.m_gpr[instr.ra]) + static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]) + static_cast<std::uint64_t>(cpu.m_xer.ca);

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;

        cpu.updateOverflow(-cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The sum ¬(RA) + CA + (-1) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void SUBFME(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t notRa = ~cpu.m_gpr[instr.ra];
        const std::uint32_t oldCarry = cpu.m_xer.ca;
        const std::uint64_t result = static_cast<std::uint64_t>(notRa) + static_cast<std::uint64_t>(oldCarry) + 0xFFFFFFFFULL;

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;

        cpu.updateOverflow(static_cast<int32_t>(notRa), static_cast<int32_t>(oldCarry) - 1, cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The sum ¬(RA) + CA is placed into register RT.
     * @param cpu
     * @param instr
     */
    void SUBFZE(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t notRa = ~cpu.m_gpr[instr.ra];
        const std::uint32_t oldCarry = cpu.m_xer.ca;
        const std::uint64_t result = static_cast<std::uint64_t>(notRa) + static_cast<std::uint64_t>(oldCarry);

        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;

        cpu.updateOverflow(static_cast<int32_t>(notRa), static_cast<int32_t>(oldCarry), cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }
}
