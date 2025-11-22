/*
** EPITECH PROJECT, 2025
** core
** File description:
** Add
*/

#include <iostream>

#include "interpreter/Interpreter.hpp"

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
        cpu.gp(instruction.rt) = cpu.gp(instruction.ra) + cpu.gp(instruction.rb);
    }

    /**
     * @brief The sum (RA) + (RB) + CA is placed into register RT.
     * @param cpu
     * @param instruction
     */
    void ADDE(Core::Interpreter &cpu, const EncodedInstruction &instruction)
    {
    }

    /**
     * @brief The sum (RA|0) + (SI || 0x0000) is placed into register RT.
     * @param cpu
     * @param instruction
     */
    void ADDIS(Core::Interpreter &cpu, const EncodedInstruction &instruction)
    {
    }
}
