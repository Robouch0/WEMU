/*
** EPITECH PROJECT, 2025
** core
** File description:
** MTSPR / MFSPR — move to/from special purpose registers
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // The 10-bit SPR field is split across the ra and rb fields:
    //   ra (bits 16-20) = spr[4:0]   (lower 5 bits)
    //   rb (bits 11-15) = spr[9:5]   (upper 5 bits)
    // Combined: spr = (rb << 5) | ra
    //
    // Common SPR numbers:
    //   1  = XER
    //   8  = LR
    //   9  = CTR

    static inline std::uint32_t decodeSPR(const EncodedInstruction &instr)
    {
        return (instr.rb << 5) | instr.ra;
    }

    // MTSPR rs, SPR — move GPR[rs] to special purpose register
    void MTSPR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t spr = decodeSPR(instr);
        switch (spr) {
            case 1: cpu.m_xer.raw = cpu.m_gpr[instr.rt]; break; // XER
            case 8: cpu.m_lr      = cpu.m_gpr[instr.rt]; break; // LR
            case 9: cpu.m_ctr     = cpu.m_gpr[instr.rt]; break; // CTR
            default: break; // unhandled SPR — silently ignore
        }
    }

    // MFSPR rt, SPR — move special purpose register to GPR[rt]
    void MFSPR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t spr = decodeSPR(instr);
        switch (spr) {
            case 1: cpu.m_gpr[instr.rt] = cpu.m_xer.raw; break; // XER
            case 8: cpu.m_gpr[instr.rt] = cpu.m_lr;      break; // LR
            case 9: cpu.m_gpr[instr.rt] = cpu.m_ctr;     break; // CTR
            default: break;
        }
    }

} // namespace Core::Instruction
