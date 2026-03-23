//
// Created by nicolas on 3/22/26.
//

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
    * @brief The contents of register RS are ORed with 480 || UI and the result is placed into register RA.
     * @param cpu
     * @param instr
     */
    void ORI(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = cpu.m_gpr[instr.rs] | instr.ui;
    }
}