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

        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The sum (RA) + (RB) + CA is placed into register RT.
     * @param cpu
     * @param instr
     */
    void ADDE(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const uint64_t result = static_cast<uint64_t>(cpu.m_gpr[instr.ra]) + static_cast<uint64_t>(cpu.m_gpr[instr.rb]) + static_cast<uint64_t>(cpu.m_xer.ca);

        cpu.m_gpr[instr.rt] = static_cast<uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;

        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], cpu.m_gprSigned[instr.rb], cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
    }

    /**
     * @brief The sum (RA) + CA - 1 is placed into register RT.
     * @param cpu
     * @param instr
     */
    void ADDME(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const uint64_t addMESubtractor = static_cast<uint64_t>(cpu.m_xer.ca) + static_cast<uint32_t>(-1); // will never cause an overflow as ca is either equal to 0 or 1
        const uint64_t result = static_cast<uint64_t>(cpu.m_gpr[instr.ra]) + addMESubtractor;

        cpu.m_gpr[instr.rt] = static_cast<uint32_t>(result);
        cpu.m_xer.ca = (result >> CARRY_OFFSET) & 1;

        cpu.updateOverflow(cpu.m_gprSigned[instr.ra], static_cast<std::int32_t>(addMESubtractor), cpu.m_gprSigned[instr.rt], instr);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.rt], instr);
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

    void ADDC(Interpreter &cpu, const EncodedInstruction &instr) {}

    void ADDI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const int32_t imm = static_cast<int16_t>(instr.si);

        if (instr.ra == 0)
            cpu.m_gpr[instr.rt] = imm;
        else
            cpu.m_gpr[instr.rt] = cpu.m_gpr[instr.ra] + imm;
    }

    void ADDIC(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDCO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDZEO(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDZE(Interpreter &cpu, const EncodedInstruction &instr) {}
    void ADDM(Interpreter &cpu, const EncodedInstruction &instr) {}
} // namespace Core::Instruction
