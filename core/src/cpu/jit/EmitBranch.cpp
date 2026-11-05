/*
** EPITECH PROJECT, 2026
** core
** File description:
** EmitBranch -- direct, conditional and register branches
*/

#include "Emitter.hpp"
#include "cpu/memory/Memory.hpp"

namespace Core::Jit {
    // Ends the block on every path. Mirrors the interpreter's LR quirks: bc and bclr set LR only
    // when taken, bcctr sets it before evaluating the condition.
    bool Emitter::emitBranch(const EncodedInstruction &i, const InstructionID id)
    {
        const std::uint32_t next = m_pc + kInstructionBytes;
        if (id == E_B || id == E_BA || id == E_BL || id == E_BLA) {
            const std::int32_t offset = static_cast<std::int32_t>(i.li << 8) >> 6;
            const bool absolute = id == E_BA || id == E_BLA;
            if (id == E_BL || id == E_BLA)
                mov(lr(), next);
            exitTo(absolute ? static_cast<std::uint32_t>(offset) - Memory::ApplicationCode : m_pc + static_cast<std::uint32_t>(offset));
            return true;
        }
        if (id != E_BC && id != E_BCLR && id != E_BCTR)
            return false;

        const std::uint32_t bo = i.rt;
        const std::uint32_t bi = i.ra;
        const bool testCtr = !(bo & 4), ctrZero = bo & 2, testCr = !(bo & 16), crValue = bo & 8;
        Xbyak::Label &notTaken = newLabel();
        if (id == E_BCTR) {
            // An import sentinel (CTR after the optional decrement) dispatches HLE through the
            // interpreter's handler.
            Xbyak::Label &import = newLabel();
            mov(eax, ctr());
            if (testCtr)
                dec(eax);
            cmp(eax, 0xC0000000u);
            jae(import, T_NEAR);
            cold([this, &import, instr = m_current, pc = m_pc, index = m_index] {
                L(import);
                calloutCall(instr, pc, index);
                mov(eax, dword[rbx + m_layout.nextPc]);
                exitIndirect();
            });
            if (i.lk)
                mov(lr(), next);
        }
        if (id == E_BCLR)
            mov(r8d, lr());
        if (testCtr) {
            dec(ctr());
            if (ctrZero)
                jnz(notTaken, T_NEAR);
            else
                jz(notTaken, T_NEAR);
        }
        if (testCr) {
            if (m_crLive)
                bt(edx, 31 - bi); // CR just written by the previous instruction
            else
                bt(cr(), 31 - bi);
            if (crValue)
                jnc(notTaken, T_NEAR);
            else
                jc(notTaken, T_NEAR);
        }
        // Taken.
        if (id != E_BCTR && i.lk)
            mov(lr(), next);
        if (id == E_BC) {
            const auto displacement = static_cast<std::int32_t>(static_cast<std::int16_t>(i.raw & 0xFFFC));
            exitTo(i.aa ? static_cast<std::uint32_t>(displacement) - Memory::ApplicationCode : m_pc + static_cast<std::uint32_t>(displacement));
        } else {
            if (id == E_BCLR) {
                mov(eax, r8d);
            } else {
                mov(eax, ctr());
                sub(eax, Memory::ApplicationCode);
            }
            exitIndirect();
        }
        L(notTaken);
        if (testCtr || testCr)
            exitTo(next);
        return true;
    }
} // namespace Core::Jit
