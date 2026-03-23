//
// Created by nicolas on 3/22/26.
//

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void AND(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] & cpu.m_gpr[instr.rb];
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }

    void ANDI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] & instr.ui;
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr, true);
    }

    void ANDIS(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] & (instr.ui << 16);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr, true);
    }

    void ANDC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] & ~(cpu.m_gpr[instr.rb]);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr, true);
    }

    void NAND(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = ~(cpu.m_gpr[instr.rs] & cpu.m_gpr[instr.rb]);
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr, true);
    }
};