/*
** EPITECH PROJECT, 2025
** core
** File description:
** Neg
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void NEG(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const bool isOverflow = (cpu.m_gprSigned[instr.ra] == INT32_MIN);

        cpu.m_gprSigned[instr.rt] = -cpu.m_gprSigned[instr.ra];
        cpu.updateOverflow(isOverflow, instr);
        cpu.updateCR0(cpu.m_gprSigned[instr.rt], instr);
    }

}
