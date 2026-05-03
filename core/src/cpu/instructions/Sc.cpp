/*
** EPITECH PROJECT, 2025
** core
** File description:
** Add
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    void SC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        Core::Symbol sym = cpu.m_binary.symbols[instr.bd];

        if (syscallHandler.syscallTable.contains(sym.name)) {
            const auto handler = syscallHandler.get(sym.name);
            handler(cpu);
        }
    }
} // namespace Core::Instruction
