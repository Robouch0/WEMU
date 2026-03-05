/*
** EPITECH PROJECT, 2025
** core
** File description:
** Add
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {
    /**
     * @brief The sum (RA) + (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void ADD(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.rt] = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The sum (RA) + (RB) + CA is placed into register RT.
     * @param cpu
     * @param instr
     */
    void ADDE(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.rt] = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb] + cpu.m_xer.ca;

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr,
                           cpu.m_xer.ca);
    }

    /**
     * @brief The sum (RA|0) + (SI || 0x0000) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void ADDIS(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.rt] = cpu.m_gpr[instr.ra] + (instr.si << 16);
    }

    // ADDC: rt = ra + rb, set CA on carry-out
    void ADDC(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(cpu.m_gpr[instr.ra])
                                   + static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
        cpu.m_xer.ca = (result >> 32) & 1;
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
    }

    // ADDI: rt = (ra|0) + sign_extend(si)
    void ADDI(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t imm = static_cast<std::int16_t>(instr.si);
        const std::int32_t base = (instr.ra != 0) ? cpu.m_gprSigned[instr.ra] : 0;
        cpu.m_gprSigned[instr.rt] = base + imm;
    }
    void ADDIC(Interpreter &cpu, const EncodedInstruction &instr) {}
    // ADDCO: rt = ra + rb, set CA on carry-out, with overflow (OE=1)
    void ADDCO(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t result = static_cast<std::uint64_t>(cpu.m_gpr[instr.ra])
                                   + static_cast<std::uint64_t>(cpu.m_gpr[instr.rb]);
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);
        cpu.m_xer.ca = (result >> 32) & 1;
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
    }
    void ADDO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDEO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDME(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDMEO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDZEO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDZE(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDM(Interpreter &cpu, const EncodedInstruction &instr) {}
} // namespace Core::Instruction
