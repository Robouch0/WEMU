/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once

#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <vector>

#include "Registers.hpp"
#include "binary/Binary.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"
#include "utils/BeDecoder.hpp"
#include "utils/Logger.hpp"

namespace Core {
    static constexpr std::uint32_t INSTR_SIZE = sizeof(std::uint32_t);

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

            template<typename T>
            T readArgs(size_t index) { return m_gpr[3 + index]; }

            void writeReturnValue(const std::uint32_t val) { m_gpr[3] = val; }

            void debugDumpGPR() const
            {
                if constexpr (Utils::Log::kLevel <= Utils::Log::Level::Trace) {
                    std::cout << "==== GPR Dump ====" << std::endl;
                    for (int i = 0; i < 32; ++i)
                        std::cout << std::format("r{:02d} : 0x{:08X}  ({})", i, m_gpr[i], m_gprSigned[i]) << std::endl;
                    std::cout << "==================" << std::endl;
                }
            }

            void reset()
            {
                m_pc = 0;
                m_nextPc = 0;
                m_cr = {};
                m_lr = 0;
                m_ctr = 0;
                std::fill(std::begin(m_gpr), std::end(m_gpr), 0u);
                m_xer = {};
                std::fill(std::begin(m_fpr), std::end(m_fpr), 0.0f);
                m_fpscr = {};
            }

        private:
            [[nodiscard]] bool step(Utils::BeDecoder &decoder, const std::uint32_t ppc_pc);

            void initInstructionMap();

            void updateCR(Core::ConditionRegister::Register &cr, const std::int32_t &result,
                          const EncodedInstruction &instr, bool forceUpdate = false) const;

            /**
             * @brief Updates XER overflow bits based on a precomputed overflow condition.
             *        Use this overload for instructions with custom overflow logic (e.g. DIVW, DIVWU).
             *
             * @param overflow Precomputed overflow condition
             * @param instr    Encoded instruction (used to check OE bit)
             */
            void updateOverflow(bool overflow, const EncodedInstruction &instr);

            /**
             * @brief Updates XER overflow bits for signed addition-based instructions.
             *        Overflow is detected when two operands of the same sign produce a result of opposite sign.
             *        Use this overload for ADD, ADDC, ADDE, ADDZE, ADDME and their variants.
             *
             * @param a      First operand (signed)
             * @param b      Second operand (signed)
             * @param result Result of the addition (signed)
             * @param instr  Encoded instruction (used to check OE bit)
             */
            void updateOverflow(const std::int32_t &a, const std::int32_t &b, const std::int32_t &result,
                const EncodedInstruction &instr);

            #define INSTR(name, ...) friend void Core::Instruction::name(Core::Interpreter &, const EncodedInstruction &);
            #include "cpu/tables/cpu_instructions.anh"
            #undef INSTR

            Core::Binary m_binary;
            Core::Memory m_memory;

            std::uint32_t m_pc {};
            std::uint32_t m_nextPc {};

            Core::ConditionRegister m_cr{};
            std::uint32_t m_lr{}; // Link Register
            std::uint32_t m_ctr{}; // Counter Register
            union {
                std::uint32_t m_gpr[32]{};
                std::int32_t m_gprSigned[32];
            }; // General Purpose Registers (unsigned/signed)
            Core::FixedPointExceptionRegister m_xer{};
            float m_fpr[32]{}; // Fixed-Point Registers
            Core::FloatingPointStatusAndControlRegister m_fpscr{};

            std::map<std::uint32_t, std::vector<InstructionInfo> > m_instructionMap{};
    };


} // namespace Core
