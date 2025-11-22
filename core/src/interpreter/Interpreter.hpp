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

#include "binary/Binary.hpp"
#include "instructions/definitions/EncodedInstruction.hpp"
#include "instructions/definitions/Instruction.hpp"

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

            std::uint32_t &gp(const std::uint32_t &idx) { return m_gpRegisters[idx]; }
            [[nodiscard]] const std::uint32_t &gp(const std::uint32_t &idx) const { return m_gpRegisters[idx]; }

        private:
            void initInstructionMap();

            Core::Binary m_binary;
            std::uint32_t m_gpRegisters[32];
            std::float32_t m_fpRegisters[32];
            std::map<std::uint32_t, std::vector<InstructionInfo>> m_instructionMap;
    };
}
