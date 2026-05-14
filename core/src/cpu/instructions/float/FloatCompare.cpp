/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatCompare
*/

#include <cmath>

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
        const float a = static_cast<float>(cpu.m_fpr[instr.fra]);
        const float b = static_cast<float>(cpu.m_fpr[instr.frb]);
        std::uint32_t condition = Core::ConditionRegisterFlag::Zero;

        if (std::isnan(a) || std::isnan(b)) {
            condition = Core::ConditionRegisterFlag::SummaryOverflow;
        } else if (a < b) {
            condition = Core::ConditionRegisterFlag::Negative;
        } else if (a > b) {
            condition = Core::ConditionRegisterFlag::Positive;
        }

        cpu.m_fpscr.fpcc = condition;
        setCRField(cpu.m_cr, instr.bf, condition);
    }

} // namespace Core::Instruction
