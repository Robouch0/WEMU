/*
** EPITECH PROJECT, 2025
** core
** File description:
** Branch instructions
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // Sign-extend the 24-bit LI field shifted left by 2 → 26-bit signed offset
    static inline std::int32_t branchOffset(const EncodedInstruction &instr)
    {
        const auto value = static_cast<std::int32_t>(instr.li << 2);
        return (value << 6) >> 6;
    }

    // Sign-extend the 14-bit BD field shifted left by 2 → 16-bit signed offset
    static inline std::int32_t bcBranchOffset(const EncodedInstruction &instr)
    {
        const auto value = static_cast<std::int32_t>(instr.bd << 2);
        return static_cast<std::int32_t>(static_cast<std::int16_t>(value));
    }

    // Evaluate branch condition from BO/BI fields per PowerPC ISA.
    // Optionally decrements CTR.
    static inline bool evalBranchCondition(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t bo = instr.bo;
        const std::uint32_t bi = instr.bi;

        // BO[0] (bit 4): if 0, decrement and test CTR
        if (!((bo >> 4) & 1))
            cpu.m_ctr--;

        // ctr_ok: BO[0] OR (CTR≠0 XOR BO[1])
        const bool ctr_ok = ((bo >> 4) & 1) || ((cpu.m_ctr != 0) ^ (bool)((bo >> 3) & 1));

        // cond_ok: BO[2] OR (CR[BI] == BO[3])
        const bool cr_bit = (cpu.m_cr.raw >> bi) & 1;
        const bool cond_ok = ((bo >> 2) & 1) || (cr_bit == (bool)((bo >> 1) & 1));

        return ctr_ok && cond_ok;
    }

    // ─── Unconditional branches (B-form, OPCD=18) ──────────────────────────────

    void B(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_nextPc = cpu.m_pc + branchOffset(instr);
    }

    void BA(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_nextPc = static_cast<std::uint32_t>(branchOffset(instr));
    }

    void BL(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_lr = cpu.m_pc + 4;
        cpu.m_nextPc = cpu.m_pc + branchOffset(instr);
    }

    void BLA(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_lr = cpu.m_pc + 4;
        cpu.m_nextPc = static_cast<std::uint32_t>(branchOffset(instr));
    }

    // ─── Conditional branches (BC-form, OPCD=16) ───────────────────────────────

    void BC(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        if (evalBranchCondition(cpu, instr))
            cpu.m_nextPc = cpu.m_pc + bcBranchOffset(instr);
    }

    void BCA(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        if (evalBranchCondition(cpu, instr))
            cpu.m_nextPc = static_cast<std::uint32_t>(bcBranchOffset(instr));
    }

    void BCL(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        if (evalBranchCondition(cpu, instr)) {
            cpu.m_lr = cpu.m_pc + 4;
            cpu.m_nextPc = cpu.m_pc + bcBranchOffset(instr);
        }
    }

    void BCLA(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        if (evalBranchCondition(cpu, instr)) {
            cpu.m_lr = cpu.m_pc + 4;
            cpu.m_nextPc = static_cast<std::uint32_t>(bcBranchOffset(instr));
        }
    }

    // ─── Branch to Link/Count Register (XL-form, OPCD=19) ─────────────────────

    // BLR — branch to link register (return from function)
    void BLR(Core::Interpreter &cpu, const EncodedInstruction &)
    {
        cpu.m_nextPc = cpu.m_lr & ~3u;
    }

    // BCCTR — branch to count register conditionally
    void BCCTR(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t bo = instr.bo;
        const std::uint32_t bi = instr.bi;

        // BCCTR never decrements CTR
        const bool cr_bit = (cpu.m_cr.raw >> bi) & 1;
        const bool cond_ok = ((bo >> 2) & 1) || (cr_bit == (bool)((bo >> 1) & 1));

        if (cond_ok)
            cpu.m_nextPc = cpu.m_ctr & ~3u;
    }

} // namespace Core::Instruction
