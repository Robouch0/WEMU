/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatCompare
*/

#include <bit>
#include <cmath>
#include <cstdint>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    static void setCRField(Core::ConditionRegister &cr, const std::uint32_t field, const std::uint32_t value)
    {
        switch (field & 0x7u) {
            case 0:
                cr.cr0 = value;
                break;
            case 1:
                cr.cr1 = value;
                break;
            case 2:
                cr.cr2 = value;
                break;
            case 3:
                cr.cr3 = value;
                break;
            case 4:
                cr.cr4 = value;
                break;
            case 5:
                cr.cr5 = value;
                break;
            case 6:
                cr.cr6 = value;
                break;
            default:
                cr.cr7 = value;
                break;
        }
    }

    static bool isSignalingNaN(const double value)
    {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        const std::uint64_t exponent = bits & 0x7FF0000000000000ull;
        const std::uint64_t fraction = bits & 0x000FFFFFFFFFFFFFull;
        const std::uint64_t quietBit = bits & 0x0008000000000000ull;

        return exponent == 0x7FF0000000000000ull && fraction != 0 && quietBit == 0;
    }

    /**
     * @brief Floating-Point Compare Unordered.
     *        Compares FRA and FRB and records the result in CR field BF.
     *        BF is a 3-bit field at raw bits [23:25] selecting CR0–CR7.
     *        Sets CR[BF].lt if FRA < FRB, CR[BF].gt if FRA > FRB,
     *        CR[BF].eq if FRA == FRB, CR[BF].so if either operand is NaN.
     *        Updates FPSCR[FPCC].
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: ra as FRA, rb as FRB; BF at raw[23:25]).
     */
    void FCMPU(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const double a = cpu.m_fpr[instr.fra];
        const double b = cpu.m_fpr[instr.frb];
        std::uint32_t condition = Core::ConditionRegisterFlag::Zero;

        if (std::isnan(a) || std::isnan(b)) {
            condition = Core::ConditionRegisterFlag::SummaryOverflow;
            cpu.m_fpscr.vxsnan = isSignalingNaN(a) || isSignalingNaN(b);
        } else if (a < b) {
            condition = Core::ConditionRegisterFlag::Negative;
        } else if (a > b) {
            condition = Core::ConditionRegisterFlag::Positive;
        }

        cpu.m_fpscr.fpcc = condition;
        setCRField(cpu.m_cr, instr.bf, condition);
    }

} // namespace Core::Instruction
