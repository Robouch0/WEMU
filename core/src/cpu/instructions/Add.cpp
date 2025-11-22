/*
** EPITECH PROJECT, 2025
** core
** File description:
** Add
*/

#include <iostream>

#include "cpu/interpreter/Interpreter.hpp"

#include "cpu/decoder/EncodedInstruction.hpp"

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
    }

    /**
     * @brief The sum (RA) + (RB) + CA is placed into register RT.
     * @param cpu
     * @param instruction
     */
    void ADDE(Core::Interpreter &cpu, const EncodedInstruction &instruction)
    {
        cpu.m_gpr[instruction.rt] = cpu.m_gpr[instruction.ra] + cpu.m_gpr[instruction.rb] + cpu.m_xer.ca;
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
