/*
** EPITECH PROJECT, 2026
** core
** File description:
** EmitSystem -- special registers, condition-register logic and no-op cache control
*/

#include "Emitter.hpp"

namespace Core::Jit {
    bool Emitter::emitSystem(const EncodedInstruction &i, const InstructionID id)
    {
        // The SPR field holds the number with its two 5-bit halves swapped.
        const std::uint32_t spr = ((i.spr >> 5) & 0x1F) | ((i.spr & 0x1F) << 5);
        switch (id) {
            case E_SYNC:
            case E_ISYNC:
            case E_EIEIO:
            case E_DCBST:
            case E_DCBF:
            case E_DCBI:
            case E_DCBT:
            case E_ICBI:
                return true;
            case E_MFCR:
                mov(eax, cr());
                mov(gpr(i.rt), eax);
                return true;
            case E_MTCRF: {
                std::uint32_t mask = 0;
                for (unsigned bit = 0; bit < 8; ++bit)
                    if ((i.fxm >> bit) & 1)
                        mask |= 0xFu << (bit * 4);
                mov(eax, gpr(i.rs));
                and_(eax, mask);
                mov(ecx, cr());
                and_(ecx, ~mask);
                or_(eax, ecx);
                mov(cr(), eax);
                return true;
            }
            case E_MFSPR:
            case E_MTSPR: {
                // Time base and processor id read the scheduler: call-out. Other unknown SPRs do
                // nothing, as in the interpreter.
                std::int32_t reg = m_layout.xer;
                if (spr >= 912 && spr <= 919)
                    reg = m_layout.gqr + static_cast<std::int32_t>((spr - 912) * 4);
                else if (spr == 8)
                    reg = m_layout.lr;
                else if (spr == 9)
                    reg = m_layout.ctr;
                else if (spr != 1)
                    return id == E_MTSPR || (spr != 268 && spr != 269 && spr != 1007);
                if (id == E_MFSPR) {
                    mov(eax, dword[rbx + reg]);
                    mov(gpr(i.rt), eax);
                } else {
                    mov(eax, gpr(i.rs));
                    mov(dword[rbx + reg], eax);
                }
                return true;
            }
            case E_MCRF: {
                const unsigned from = (i.raw >> 18) & 7;
                mov(ecx, cr());
                shr(ecx, static_cast<int>((7 - from) * 4));
                and_(ecx, 0xF);
                setCrField((i.raw >> 23) & 7);
                return true;
            }
            case E_CRAND:
            case E_CROR:
            case E_CRXOR:
            case E_CRNAND:
            case E_CRNOR:
            case E_CRANDC:
            case E_CRORC:
            case E_CREQV: {
                // CR bit n lives at raw bit 31 - n.
                mov(eax, cr());
                bt(eax, 31 - i.ra);
                setc(cl);
                bt(eax, 31 - i.rb);
                setc(dl);
                if (id == E_CRANDC || id == E_CRORC)
                    xor_(dl, 1);
                if (id == E_CRAND || id == E_CRNAND || id == E_CRANDC)
                    and_(cl, dl);
                else if (id == E_CROR || id == E_CRNOR || id == E_CRORC)
                    or_(cl, dl);
                else
                    xor_(cl, dl);
                if (id == E_CRNAND || id == E_CRNOR || id == E_CREQV)
                    xor_(cl, 1);
                movzx(ecx, cl);
                btr(eax, 31 - i.rt);
                shl(ecx, 31 - i.rt);
                or_(eax, ecx);
                mov(cr(), eax);
                return true;
            }
            default:
                return false;
        }
    }
} // namespace Core::Jit
