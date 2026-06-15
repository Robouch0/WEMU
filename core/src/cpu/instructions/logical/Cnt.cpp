//
// Created by nicolas on 4/16/26.
//

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void CNTLZW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        std::uint8_t n;

        for (n = 0; n < 32; n++) {
            if (cpu.m_gpr[instr.rs] >> (31 - n) == 1)
                break;
        }

        cpu.m_gpr[instr.ra] = n;
        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }

} // namespace Core::Instruction
