/*
** EPITECH PROJECT, 2026
** core
** File description:
** Spr
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // The SPR field is stored split: the two 5-bit halves are swapped relative to the SPR number.
    static inline std::uint32_t decodeSpr(const std::uint32_t field) { return ((field >> 5) & 0x1F) | ((field & 0x1F) << 5); }

    /**
     * @brief Move From Special Purpose Register.
     *        RT = SPR. Supported: XER (1), LR (8), CTR (9), GQR0-7 (912-919), TBL/TBU (268/269).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb).
     */
    void MFSPR(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t spr = decodeSpr(instr.spr);

        if (spr >= 912 && spr <= 919) {
            cpu.m_gpr[instr.rt] = cpu.m_gqr[spr - 912];
            return;
        }
        switch (spr) {
            case 1:
                cpu.m_gpr[instr.rt] = cpu.m_xer.raw;
                break;
            case 8:
                cpu.m_gpr[instr.rt] = cpu.m_lr;
                break;
            case 9:
                cpu.m_gpr[instr.rt] = cpu.m_ctr;
                break;
            case 268: // TBL
                cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(cpu.m_scheduler.now());
                break;
            case 269: // TBU
                cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(cpu.m_scheduler.now() >> 32);
                break;
            case 1007: // UPIR: user-mode processor ID, consistent with OSGetCoreId
                cpu.m_gpr[instr.rt] = cpu.m_scheduler.currentCoreId();
                break;
            default:
                break;
        }
    }

    /**
     * @brief Move To Special Purpose Register.
     *        SPR = RS. Supported: XER (1), LR (8), CTR (9), GQR0-7 (912-919).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra, rb).
     */
    void MTSPR(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t spr = decodeSpr(instr.spr);

        if (spr >= 912 && spr <= 919) {
            cpu.m_gqr[spr - 912] = cpu.m_gpr[instr.rs];
            return;
        }
        switch (spr) {
            case 1:
                cpu.m_xer.raw = cpu.m_gpr[instr.rs];
                break;
            case 8:
                cpu.m_lr = cpu.m_gpr[instr.rs];
                break;
            case 9:
                cpu.m_ctr = cpu.m_gpr[instr.rs];
                break;
            default:
                break;
        }
    }

    /**
     * @brief Move From Condition Register.
     *        RT = CR. Copies all 32 bits of the condition register into RT.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt).
     */
    void MFCR(Interpreter &cpu, const EncodedInstruction &instr) { cpu.m_gpr[instr.rt] = cpu.m_cr.raw; }

    /**
     * @brief Move To Condition Register Fields.
     *        Selected CR fields = RS[0:31] masked by FXM.
     *        FXM is an 8-bit field at bits [12:19]; bit 7 selects CR0, bit 0 selects CR7.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS; FXM at raw bits [12:19]).
     */
    void MTCRF(Interpreter &cpu, const EncodedInstruction &instr)
    {
        std::uint32_t mask = 0;

        for (std::size_t bit = 0; bit < 8; bit++) {
            if ((instr.fxm >> bit) & 1)
                mask |= 0b1111 << (bit * 4);
        }
        cpu.m_cr.raw = (cpu.m_gpr[instr.rs] & mask) | (cpu.m_cr.raw & ~mask);
    }

} // namespace Core::Instruction
