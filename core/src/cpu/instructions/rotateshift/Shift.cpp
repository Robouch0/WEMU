//
// Created by nicolas on 4/16/26.
//


#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void SRAW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint8_t n = cpu.m_gpr[instr.rb] & 0x1F;
        const std::uint32_t r = cpu.m_gpr[instr.rs] << (32 - n) | cpu.m_gpr[instr.rs] >> n;
        const std::uint32_t m = (cpu.m_gpr[instr.rb] >> 5 & 1) == 0 ? 0XFFFFFFFF >> n : 0;
        const std::uint32_t s = cpu.m_gpr[instr.rs] >> 31;

        cpu.m_gpr[instr.ra] = r & m | (s ? 0xFFFFFFFF : s) & ~m;
        cpu.m_xer.ca = s & ((r & ~m) != 0);

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }

    void SRAWI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint8_t n = instr.sh;
        const std::uint32_t r = cpu.m_gpr[instr.rs] << (32 - n) | cpu.m_gpr[instr.rs] >> n;
        const std::uint32_t m = 0XFFFFFFFF >> n;
        const std::uint32_t s = cpu.m_gpr[instr.rs] >> 31;

        cpu.m_gpr[instr.ra] = r & m | (s ? 0xFFFFFFFF : s) & ~m;
        cpu.m_xer.ca = s & ((r & ~m) != 0);

        cpu.updateCR(cpu.m_cr.cr0, cpu.m_gprSigned[instr.ra], instr);
    }

}