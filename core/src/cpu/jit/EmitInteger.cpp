/*
** EPITECH PROJECT, 2026
** core
** File description:
** EmitInteger -- integer arithmetic, logic, rotations and compares
*/

#include "Emitter.hpp"

namespace Core::Jit {
    namespace {
        // Interpreter's rotlMask: bits mb..me inclusive, wrapping when mb > me.
        constexpr std::uint32_t rotateMask(const std::uint32_t mb, const std::uint32_t me)
        {
            std::uint32_t mask = 0;
            for (std::uint32_t i = mb; i != me; i = (i + 1) % 32)
                mask |= 1u << (31 - i);
            return mask | 1u << (31 - me);
        }

        // XO-form arithmetic whose OE variant updates XER[OV]/[SO]: kept on the call-out.
        constexpr bool overflowForm(const InstructionID id)
        {
            switch (id) {
                case E_ADD:
                case E_ADDC:
                case E_ADDE:
                case E_ADDZE:
                case E_ADDME:
                case E_SUBF:
                case E_SUBFC:
                case E_SUBFE:
                case E_SUBFZE:
                case E_SUBFME:
                case E_NEG:
                case E_MULLW:
                case E_MULHW:
                case E_MULHWU:
                case E_DIVW:
                case E_DIVWU:
                    return true;
                default:
                    return false;
            }
        }
    } // namespace

    bool Emitter::emitInteger(const EncodedInstruction &i, const InstructionID id)
    {
        if (overflowForm(id) && i.oe)
            return false;
        const auto simm = static_cast<std::int32_t>(static_cast<std::int16_t>(i.si));
        const bool alu = m_layout.nativeMask & FamilyInteger;
        const bool compare = m_layout.nativeMask & FamilyCompare;
        // Result in eax: store it in `rd`, then CR0 for record forms.
        const auto result = [&](const unsigned rd, const bool record) {
            mov(gpr(rd), eax);
            if (record)
                recordCr0();
        };
        // Signed (LT/GT) or unsigned (below/above) compare already in the host flags.
        const auto compareField = [&](const bool isSigned) {
            mov(ecx, 2);
            mov(edx, 4);
            if (isSigned)
                cmovg(ecx, edx);
            else
                cmova(ecx, edx);
            mov(edx, 8);
            if (isSigned)
                cmovl(ecx, edx);
            else
                cmovb(ecx, edx);
            mov(edx, xer());
            and_(edx, 1);
            or_(ecx, edx);
            setCrField(i.rt >> 2);
            m_crRecorded = true;
        };

        switch (id) {
            case E_CMP:
            case E_CMPL:
            case E_CMPI:
            case E_CMPLI:
                if (!compare)
                    return false;
                mov(eax, gpr(i.ra));
                if (id == E_CMP || id == E_CMPL)
                    cmp(eax, gpr(i.rb));
                else
                    cmp(eax, id == E_CMPI ? static_cast<std::uint32_t>(simm) : i.si & 0xFFFFu);
                compareField(id == E_CMP || id == E_CMPI);
                return true;
            default:
                break;
        }
        if (!alu)
            return false;

        switch (id) {
            case E_ADDI:
            case E_ADDIS: {
                const std::uint32_t imm = id == E_ADDI ? static_cast<std::uint32_t>(simm) : i.si << 16;
                if (i.ra == 0) {
                    mov(gpr(i.rt), imm);
                } else {
                    mov(eax, gpr(i.ra));
                    if (imm)
                        add(eax, imm);
                    mov(gpr(i.rt), eax);
                }
                return true;
            }
            case E_ADD:
                mov(eax, gpr(i.ra));
                add(eax, gpr(i.rb));
                result(i.rt, i.rc);
                return true;
            case E_ADDC:
                mov(eax, gpr(i.ra));
                add(eax, gpr(i.rb));
                setCarry();
                result(i.rt, i.rc);
                return true;
            case E_ADDE:
            case E_ADDZE:
            case E_ADDME:
                mov(eax, gpr(i.ra));
                loadCarry();
                if (id == E_ADDE)
                    adc(eax, gpr(i.rb));
                else
                    adc(eax, id == E_ADDZE ? 0 : -1);
                setCarry();
                result(i.rt, i.rc);
                return true;
            case E_ADDIC:
            case E_ADDIC_:
                mov(eax, gpr(i.ra));
                add(eax, static_cast<std::uint32_t>(simm));
                setCarry();
                result(i.rt, id == E_ADDIC_);
                return true;
            case E_SUBF:
            case E_SUBFC:
                mov(eax, gpr(i.rb));
                sub(eax, gpr(i.ra));
                if (id == E_SUBFC)
                    setCarry(true); // CA is the absence of a borrow
                result(i.rt, i.rc);
                return true;
            case E_SUBFIC:
                mov(eax, static_cast<std::uint32_t>(simm));
                sub(eax, gpr(i.ra));
                setCarry(true);
                result(i.rt, false);
                return true;
            case E_SUBFE:
            case E_SUBFZE:
            case E_SUBFME:
                mov(eax, gpr(i.ra));
                not_(eax);
                loadCarry();
                if (id == E_SUBFE)
                    adc(eax, gpr(i.rb));
                else
                    adc(eax, id == E_SUBFZE ? 0 : -1);
                setCarry();
                result(i.rt, i.rc);
                return true;
            case E_NEG:
                mov(eax, gpr(i.ra));
                neg(eax);
                result(i.rt, i.rc);
                return true;
            case E_MULLI:
                imul(eax, gpr(i.ra), simm);
                result(i.rt, false);
                return true;
            case E_MULLW:
                mov(eax, gpr(i.ra));
                imul(eax, gpr(i.rb));
                result(i.rt, i.rc);
                return true;
            case E_MULHW:
            case E_MULHWU:
                mov(eax, gpr(i.ra));
                if (id == E_MULHW)
                    imul(gpr(i.rb));
                else
                    mul(gpr(i.rb));
                mov(eax, edx);
                result(i.rt, i.rc);
                return true;
            case E_DIVW:
            case E_DIVWU: {
                // Division by zero (and INT_MIN / -1) leaves RT unchanged, as in the interpreter.
                Xbyak::Label skip, divide;
                mov(ecx, gpr(i.rb));
                mov(eax, gpr(i.ra));
                test(ecx, ecx);
                jz(skip);
                if (id == E_DIVW) {
                    cmp(ecx, -1);
                    jne(divide);
                    cmp(eax, 0x80000000u);
                    je(skip);
                    L(divide);
                    cdq();
                    idiv(ecx);
                } else {
                    xor_(edx, edx);
                    div(ecx);
                }
                mov(gpr(i.rt), eax);
                L(skip);
                if (i.rc) {
                    mov(eax, gpr(i.rt));
                    recordCr0();
                }
                return true;
            }
            case E_AND:
            case E_ANDC:
            case E_OR:
            case E_ORC:
            case E_NOR:
            case E_NAND:
            case E_XOR:
            case E_EQV:
                mov(eax, gpr(i.rs));
                if (id == E_ANDC || id == E_ORC) {
                    mov(ecx, gpr(i.rb));
                    not_(ecx);
                    if (id == E_ANDC)
                        and_(eax, ecx);
                    else
                        or_(eax, ecx);
                } else if (id == E_AND || id == E_NAND) {
                    and_(eax, gpr(i.rb));
                } else if (id == E_OR || id == E_NOR) {
                    or_(eax, gpr(i.rb));
                } else {
                    xor_(eax, gpr(i.rb));
                }
                if (id == E_NOR || id == E_NAND || id == E_EQV)
                    not_(eax);
                result(i.ra, i.rc);
                return true;
            case E_ORI:
            case E_ORIS:
            case E_XORI:
            case E_XORIS:
            case E_ANDI:
            case E_ANDIS: {
                const std::uint32_t imm = (id == E_ORIS || id == E_XORIS || id == E_ANDIS) ? i.ui << 16 : i.ui;
                if ((id == E_ORI || id == E_ORIS || id == E_XORI || id == E_XORIS) && !imm && i.rs == i.ra)
                    return true; // nop
                mov(eax, gpr(i.rs));
                if (id == E_ORI || id == E_ORIS)
                    or_(eax, imm);
                else if (id == E_XORI || id == E_XORIS)
                    xor_(eax, imm);
                else
                    and_(eax, imm);
                result(i.ra, id == E_ANDI || id == E_ANDIS);
                return true;
            }
            case E_EXTSB:
                movsx(eax, byte[rbx + static_cast<int>(i.rt * 4)]);
                result(i.ra, i.rc);
                return true;
            case E_EXTSH:
                movsx(eax, word[rbx + static_cast<int>(i.rt * 4)]);
                result(i.ra, i.rc);
                return true;
            case E_CNTLZW:
                lzcnt(eax, gpr(i.rt));
                result(i.ra, i.rc);
                return true;
            case E_SLW:
            case E_SRW:
                // Shift amounts 32-63 give zero.
                mov(ecx, gpr(i.rb));
                mov(eax, gpr(i.rs));
                and_(ecx, 63);
                if (id == E_SLW)
                    shlx(eax, eax, ecx);
                else
                    shrx(eax, eax, ecx);
                xor_(edx, edx);
                cmp(ecx, 32);
                cmovae(eax, edx);
                result(i.ra, i.rc);
                return true;
            case E_SRAW: {
                // CA = negative source with one bits shifted out; amounts 32-63 give the sign.
                Xbyak::Label large, done;
                mov(ecx, gpr(i.rb));
                mov(eax, gpr(i.rs));
                test(ecx, 0x20);
                jnz(large);
                and_(ecx, 31);
                bzhi(edx, eax, ecx);
                sar(eax, cl);
                test(edx, edx);
                setnz(dl);
                test(eax, eax);
                sets(cl);
                and_(cl, dl);
                jmp(done);
                L(large);
                sar(eax, 31);
                mov(ecx, eax);
                and_(ecx, 1);
                L(done);
                movzx(ecx, cl);
                mov(edx, xer());
                and_(edx, ~4u);
                lea(edx, ptr[rdx + rcx * 4]);
                mov(xer(), edx);
                result(i.ra, i.rc);
                return true;
            }
            case E_SRAWI: {
                const unsigned n = i.sh;
                mov(eax, gpr(i.rs));
                xor_(ecx, ecx);
                if (n) {
                    test(eax, (1u << n) - 1);
                    setnz(dl);
                    sar(eax, static_cast<int>(n));
                    sets(cl);
                    and_(cl, dl);
                }
                mov(edx, xer());
                and_(edx, ~4u);
                lea(edx, ptr[rdx + rcx * 4]);
                mov(xer(), edx);
                result(i.ra, i.rc);
                return true;
            }
            case E_RLWINM:
            case E_RLWNM:
            case E_RLWIMI: {
                const std::uint32_t mask = rotateMask(i.mb, i.me);
                mov(eax, gpr(i.rs));
                if (id == E_RLWNM) {
                    mov(ecx, gpr(i.rb));
                    rol(eax, cl);
                } else if (i.sh) {
                    rol(eax, i.sh);
                }
                if (mask != ~0u)
                    and_(eax, mask);
                if (id == E_RLWIMI) {
                    mov(ecx, gpr(i.ra));
                    and_(ecx, ~mask);
                    or_(eax, ecx);
                }
                result(i.ra, i.rc);
                return true;
            }
            default:
                return false;
        }
    }
} // namespace Core::Jit
