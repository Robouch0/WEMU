/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once

#include <cstdint>
#include <stdfloat>
#include <map>
#include <vector>

#include "Registers.hpp"
#include "binary/Binary.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"

namespace Core
{
    class InterpreterException final : public Core::Exception
    {
        public:
        explicit InterpreterException(const std::string &errorMessage) : Core::Exception("InterpreterException", errorMessage) {}
        ~InterpreterException() override = default;
    };

    class Interpreter
    {
        public:
            explicit Interpreter(Core::Binary binary);
            ~Interpreter() = default;

            void run();

            InstructionID findInstructionID(const EncodedInstruction &instr);

            void executeInstruction(const EncodedInstruction &instr);

        private:
            void initInstructionMap();

            #define INSTR(name, ...) friend void Core::Instruction::name(Core::Interpreter&, const EncodedInstruction&);
                #include "cpu/tables/cpu_instructions.anh"
            #undef INSTR

            Core::Binary m_binary;

            Core::ConditionRegister m_cr;
            std::uint32_t m_lr;                                             // Link Register
            std::uint32_t m_ctr;                                            // Counter Register
            union { std::uint32_t m_gpr[32]; std::int32_t m_gprSigned[32]; };   // General Purpose Registers (unsigned/signed)
            Core::FixedPointExceptionRegister m_xer;
            std::float32_t m_fpr[32];                                       // Fixed-Point Registers
            Core::FloatingPointStatusAndControlRegister m_fpscr;

            std::map<std::uint32_t, std::vector<InstructionInfo>> m_instructionMap;
    };
}
