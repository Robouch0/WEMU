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

#include "Instruction.hpp"
#include "binary/Binary.hpp"

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

            InstructionID findInstructionID(const EncodedInstruction &encodedInstr);

        private:
            void initInstructionMap();

            Core::Binary m_binary;
            std::uint32_t m_gpRegisters[32];
            std::float32_t m_fpRegisters[32];
            std::map<std::uint32_t, std::vector<InstructionInfo>> m_instructionMap;
    };
}
