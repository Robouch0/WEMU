/*
** EPITECH PROJECT, 2026
** core
** File description:
** Spr
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Move From Special Purpose Register.
     *        RT = SPR. SPR is encoded as (inst.rb << 5) | inst.ra.
     *        Supported SPRs: XER (1), LR (8), CTR (9).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb).
     */
    void MFSPR(Interpreter &cpu, const EncodedInstruction &instr)
    {
        switch (instr.spr) {
            case 0b00001'00000:
                cpu.m_gpr[instr.rt] = cpu.m_xer.raw;
                break;
            case 0b01000'00000:
                cpu.m_gpr[instr.rt] = cpu.m_lr;
                break;
            case 0b01001'00000:
                cpu.m_gpr[instr.rt] = cpu.m_ctr;
                break;
            default:
                break;
        }
    }

    /**
     * @brief Move To Special Purpose Register.
     *        SPR = RS. SPR is encoded as (inst.rb << 5) | inst.ra.
     *        Supported SPRs: XER (1), LR (8), CTR (9).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra, rb).
     */
    void MTSPR(Interpreter &cpu, const EncodedInstruction &instr)
    {
        switch (instr.spr) {
            case 0b00001'00000:
                cpu.m_xer.raw = cpu.m_gpr[instr.rs];
                break;
            case 0b01000'00000:
                cpu.m_lr = cpu.m_gpr[instr.rs];
                break;
            case 0b01001'00000:
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
