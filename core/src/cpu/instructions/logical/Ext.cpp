//
// Created by nicolas on 4/9/26.
//


#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void EXTSB(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint8_t s = cpu.m_gpr[instr.rs] >> 7 & 1;
        const std::uint32_t valueRs = static_cast<std::uint8_t>(cpu.m_gpr[instr.rs]);
        cpu.m_gpr[instr.ra] = s ? 0xFFFFFF00 | valueRs : valueRs;

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }

    void EXTSH(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint8_t s = cpu.m_gpr[instr.rs] >> 15 & 1;
        const std::uint32_t valueRs = static_cast<std::uint16_t>(cpu.m_gpr[instr.rs]);
        cpu.m_gpr[instr.ra] = s ? 0xFFFF0000 | valueRs : valueRs;

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }

}
