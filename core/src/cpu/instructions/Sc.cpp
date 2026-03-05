/*
** EPITECH PROJECT, 2025
** core
** File description:
** SC — system call / HLE dispatch
*/

#include <iostream>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // Plan step 6: log missing handlers so silence is replaced by diagnostic output
    void SC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        if (instr.bd >= cpu.m_binary.symbols.size()) {
            std::cerr << "[SC] invalid symbol index " << instr.bd << std::endl;
            return;
        }

        const Core::Symbol &sym = cpu.m_binary.symbols[instr.bd];

        if (Core::syscallHandler.syscallTable.contains(sym.name)) {
            Core::syscallHandler.get(sym.name)(cpu);
        } else {
            std::cerr << "[SC] unhandled HLE call: " << sym.name
                      << " (index=" << instr.bd << ")" << std::endl;
        }
    }

} // namespace Core::Instruction
