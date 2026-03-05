/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once

#include <cstdint>
#include <iomanip>
#include <map>
#include <stdfloat>
#include <type_traits>
#include <vector>

#include "Registers.hpp"
#include "binary/Binary.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"

namespace Core {
    class InterpreterException final : public Core::Exception {
        public:
            explicit InterpreterException(const std::string &errorMessage) : Core::Exception(
                "InterpreterException", errorMessage) {}

            ~InterpreterException() override = default;
    };

    class Interpreter {
        public:
            explicit Interpreter(Core::Binary binary);

            ~Interpreter() = default;

            void run();

            InstructionID findInstructionID(const EncodedInstruction &instr);

            void executeInstruction(const EncodedInstruction &instr);

            // Plan step 4: readArgs<T> — translate virtual pointer if T is a pointer type
            template<typename T>
            T readArgs(std::size_t index)
            {
                const std::uint32_t raw = m_gpr[3 + index];
                if constexpr (std::is_pointer_v<T>) {
                    const std::size_t host = m_binary.m_memory.translate(raw);
                    return reinterpret_cast<T>(host);
                } else {
                    return static_cast<T>(raw);
                }
            }

            void debugDumpGPR() const
            {
                std::ios oldState(nullptr);
                oldState.copyfmt(std::cout);
                std::cout << "==== GPR Dump ====\n";
                for (int i = 0; i < 32; ++i) {
                    std::cout
                        << "r" << std::setw(2) << std::setfill('0') << i << " : "
                        << "0x" << std::hex << std::setw(8) << std::setfill('0') << m_gpr[i]
                        << "  (" << std::dec << m_gprSigned[i] << ")\n";
                }
                std::cout << "==================\n";
                std::cout.copyfmt(oldState);
            }

        public:
            void initInstructionMap();

            void updateCR(Core::ConditionRegister::Register &cr, const std::int32_t &result,
                          const EncodedInstruction &instr) const;

            void updateOverflow(const std::int32_t &a, const std::int32_t &b, const std::int32_t &result,
                                const EncodedInstruction &instr, const std::uint32_t &carry = 0);

            #define INSTR(name, ...) friend void Core::Instruction::name(Core::Interpreter &, const EncodedInstruction &);
            #include "cpu/tables/cpu_instructions.anh"
            #undef INSTR

            Core::Binary m_binary;

            std::uint32_t m_pc {};
            std::uint32_t m_nextPc {};

            Core::ConditionRegister m_cr{};
            std::uint32_t m_lr{};
            std::uint32_t m_ctr{};
            union {
                std::uint32_t m_gpr[32]{};
                std::int32_t m_gprSigned[32];
            };
            Core::FixedPointExceptionRegister m_xer{};
            std::float32_t m_fpr[32]{};
            Core::FloatingPointStatusAndControlRegister m_fpscr{};

            std::map<std::uint32_t, std::vector<InstructionInfo> > m_instructionMap{};
    };
} // namespace Core
