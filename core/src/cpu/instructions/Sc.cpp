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
    void SC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        Core::Symbol sym = cpu.m_binary.symbols[instr.bd];

        std::cout << "SC[" << sym.name << "]" << std::endl;
    }
}
