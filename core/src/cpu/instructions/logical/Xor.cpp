//
// Created by nicolas on 3/22/26.
//

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void XOR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] ^ cpu.m_gpr[instr.rb];
        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }

    void XORI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] ^ cpu.m_gpr[instr.ui];
    }

    void XORIS(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t rightComparison = cpu.m_gpr[instr.ui] << 16;
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] ^ rightComparison;
    }
}