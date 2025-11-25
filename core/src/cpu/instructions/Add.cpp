/*
** EPITECH PROJECT, 2025
** core
** File description:
** Add
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction
{

    /**
     * @brief The sum (RA) + (RB) is placed into register RT.
     * @param cpu
     * @param instruction
     */
    void ADD(Core::Interpreter &cpu, const EncodedInstruction &instruction)
    {
        cpu.m_gpr[instruction.rt] = cpu.m_gpr[instruction.ra] + cpu.m_gpr[instruction.rb];

        if (instruction.rc)
            cpu.m_cr.cr0.raw = ConditionRegister::update(cpu.m_xer.so, cpu.m_gprSigned[instruction.rt]);
    }

    /**
     * @brief The sum (RA) + (RB) + CA is placed into register RT.
     * @param cpu
     * @param instruction
     */
    void ADDE(Core::Interpreter &cpu, const EncodedInstruction &instruction)
    {
        cpu.m_gpr[instruction.rt] = cpu.m_gpr[instruction.ra] + cpu.m_gpr[instruction.rb] + cpu.m_xer.ca;

        if (instruction.rc)
            cpu.m_cr.cr0.raw = ConditionRegister::update(cpu.m_xer.so, cpu.m_gprSigned[instruction.rt]);
    }

    /**
     * @brief The sum (RA|0) + (SI || 0x0000) is placed into register RT.
     * @param cpu
     * @param instruction
     */
    void ADDIS(Core::Interpreter &cpu, const EncodedInstruction &instruction)
    {
        cpu.m_gpr[instruction.rt] = cpu.m_gpr[instruction.ra] + (instruction.si << 16);
    }
}
