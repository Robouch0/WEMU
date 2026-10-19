/*
** EPITECH PROJECT, 2026
** core
** File description:
** EmitFloat -- floating-point and paired-single instructions
*/

#include "Emitter.hpp"

namespace Core::Jit {
    namespace {
        constexpr std::uint64_t kSignBit = 0x8000000000000000ull;
        constexpr std::uint64_t kOne = 0x3FF0000000000000ull; // 1.0
        constexpr std::uint64_t kIntMax = 0x41DFFFFFFFC00000ull; // 2147483647.0

        enum class Lane { Ps0, Ps1 };

        // Paired-single arithmetic applied to both lanes (Interpreter pairLanes).
        constexpr bool pairedArithmetic(const InstructionID id)
        {
            switch (id) {
                case E_PS_ADD:
                case E_PS_SUB:
                case E_PS_MUL:
                case E_PS_DIV:
                case E_PS_MADD:
                case E_PS_MSUB:
                case E_PS_NMADD:
                case E_PS_NMSUB:
                case E_PS_RES:
                case E_PS_RSQRTE:
                    return true;
                default:
                    return false;
            }
        }
    } // namespace

    void Emitter::recordCr1(const EncodedInstruction &i)
    {
        // Interpreter::updateCR1: FX, FEX, VX, OX are FPSCR's top four bits, in CR order.
        if (!i.rc)
            return;
        mov(ecx, dword[rbx + m_layout.fpscr]);
        shr(ecx, 28);
        setCrField(1);
    }

    void Emitter::roundToSingle(const Xbyak::Xmm &x)
    {
        vcvtsd2ss(x, x, x);
        vcvtss2sd(x, x, x);
    }

    // xmm0 = op(a, b, c) for one lane, as the interpreter computes it in double precision.
    void Emitter::arithmetic(const InstructionID id, const Xbyak::Address &a, const Xbyak::Address &b, const Xbyak::Address &c)
    {
        switch (id) {
            case E_FADD:
            case E_FADDS:
            case E_PS_ADD:
                vmovsd(xmm0, a);
                vaddsd(xmm0, xmm0, b);
                break;
            case E_FSUB:
            case E_FSUBS:
            case E_PS_SUB:
                vmovsd(xmm0, a);
                vsubsd(xmm0, xmm0, b);
                break;
            case E_FMUL:
            case E_FMULS:
            case E_PS_MUL:
                vmovsd(xmm0, a);
                vmulsd(xmm0, xmm0, c);
                break;
            case E_FDIV:
            case E_FDIVS:
            case E_PS_DIV:
                vmovsd(xmm0, a);
                vdivsd(xmm0, xmm0, b);
                break;
            case E_FSQRT:
                vmovsd(xmm0, b);
                vsqrtsd(xmm0, xmm0, xmm0);
                break;
            case E_FRSQRTE:
            case E_PS_RSQRTE:
                vmovsd(xmm1, b);
                vsqrtsd(xmm1, xmm1, xmm1);
                mov(rax, kOne);
                vmovq(xmm0, rax);
                vdivsd(xmm0, xmm0, xmm1);
                break;
            case E_FRES:
            case E_PS_RES:
                mov(rax, kOne);
                vmovq(xmm0, rax);
                vdivsd(xmm0, xmm0, b);
                break;
            default: {
                // Fused forms: std::fma(a, c, +-b), optionally negated. NaN results replay the
                // interpreter's handler (guardNaN), so only the value matters here.
                const bool negateAddend = id == E_FMSUB || id == E_FMSUBS || id == E_FNMSUB || id == E_FNMSUBS || id == E_PS_MSUB || id == E_PS_NMSUB;
                const bool negateResult =
                        id == E_FNMADD || id == E_FNMADDS || id == E_FNMSUB || id == E_FNMSUBS || id == E_PS_NMADD || id == E_PS_NMSUB;
                vmovsd(xmm0, a);
                vmovsd(xmm1, c);
                vmovsd(xmm2, b);
                if (negateAddend)
                    flipSign(xmm2);
                vfmadd132sd(xmm0, xmm2, xmm1); // a * c + b
                if (negateResult)
                    flipSign(xmm0);
                break;
            }
        }
    }

    Xbyak::Label &Emitter::guardNaN(const std::initializer_list<Xbyak::Xmm> results)
    {
        // Which NaN an operation returns depends on how the compiler ordered the interpreter's
        // operands, so a NaN result replays the instruction through its handler instead. Nothing
        // is written before this check.
        Xbyak::Label &replay = newLabel();
        Xbyak::Label &done = newLabel();
        for (const auto &x: results) {
            vucomisd(x, x);
            jp(replay, T_NEAR);
        }
        cold([this, &replay, &done, instr = m_current, pc = m_pc, index = m_index] {
            L(replay);
            calloutCall(instr, pc, index);
            jmp(done, T_NEAR);
        });
        return done;
    }

    void Emitter::flipSign(const Xbyak::Xmm &x)
    {
        mov(rax, kSignBit);
        vmovq(xmm5, rax);
        vxorpd(x, x, xmm5);
    }

    bool Emitter::emitFloat(const EncodedInstruction &i, const InstructionID id)
    {
        const auto single = [&] {
            roundToSingle(xmm0);
            vmovsd(fpr(i.frt), xmm0);
            vmovsd(ps1(i.frt), xmm0);
        };
        switch (id) {
            case E_FADD:
            case E_FSUB:
            case E_FMUL:
            case E_FDIV:
            case E_FSQRT:
            case E_FRSQRTE:
            case E_FMADD:
            case E_FMSUB:
            case E_FNMADD:
            case E_FNMSUB: {
                arithmetic(id, fpr(i.fra), fpr(i.frb), fpr(i.frc));
                Xbyak::Label &done = guardNaN({xmm0});
                vmovsd(fpr(i.frt), xmm0);
                recordCr1(i);
                L(done);
                return true;
            }
            case E_FADDS:
            case E_FSUBS:
            case E_FMULS:
            case E_FDIVS:
            case E_FRES:
            case E_FMADDS:
            case E_FMSUBS:
            case E_FNMADDS:
            case E_FNMSUBS: {
                // Single results fill both paired-single lanes.
                arithmetic(id, fpr(i.fra), fpr(i.frb), fpr(i.frc));
                Xbyak::Label &done = guardNaN({xmm0});
                single();
                recordCr1(i);
                L(done);
                return true;
            }
            case E_FRSP:
                vmovsd(xmm0, fpr(i.frb));
                single();
                recordCr1(i);
                return true;
            case E_FMR:
            case E_FNEG:
            case E_FABS:
            case E_FNABS:
                mov(rax, fpr(i.frb));
                if (id == E_FNEG)
                    btc(rax, 63);
                else if (id == E_FABS)
                    btr(rax, 63);
                else if (id == E_FNABS)
                    bts(rax, 63);
                mov(fpr(i.frt), rax);
                recordCr1(i);
                return true;
            case E_FSEL: {
                // FRT = (FRA >= 0 and not NaN) ? FRC : FRB; unordered sets CF.
                Xbyak::Label keep;
                vmovsd(xmm0, fpr(i.fra));
                vxorpd(xmm1, xmm1, xmm1);
                mov(rax, fpr(i.frb));
                vucomisd(xmm0, xmm1);
                jb(keep);
                mov(rax, fpr(i.frc));
                L(keep);
                mov(fpr(i.frt), rax);
                recordCr1(i);
                return true;
            }
            case E_FCTIW:
            case E_FCTIWZ: {
                // toInt32Clamped: NaN and values below INT_MIN give 0x80000000 (the host's integer
                // indefinite), values at or above INT_MAX give 0x7FFFFFFF.
                Xbyak::Label inRange;
                vmovsd(xmm0, fpr(i.frb));
                if (id == E_FCTIW)
                    vcvtsd2si(eax, xmm0); // round to nearest, as std::nearbyint
                else
                    vcvttsd2si(eax, xmm0);
                mov(rcx, kIntMax);
                vmovq(xmm1, rcx);
                vucomisd(xmm0, xmm1);
                jb(inRange);
                mov(eax, 0x7FFFFFFF);
                L(inRange);
                mov(rcx, 0xFFF8000000000000ull);
                or_(rax, rcx);
                mov(fpr(i.frt), rax);
                recordCr1(i);
                return true;
            }
            case E_FCMPU:
                compareFloat(fpr(i.fra), fpr(i.frb), i.bf, false);
                return true;
            default:
                return emitFloatMemory(i, id);
        }
    }

    // CR[bf] and FPSCR[FPCC] from a compare; NaN operands set SO and FPSCR[VXSNAN] (and VXVC
    // for ordered compares), as Interpreter's FCMPU and PS_CMPO0.
    void Emitter::compareFloat(const Xbyak::Address &a, const Xbyak::Address &b, const unsigned field, const bool ordered)
    {
        Xbyak::Label &unordered = newLabel();
        Xbyak::Label &done = newLabel();
        vmovsd(xmm0, a);
        vucomisd(xmm0, b);
        jp(unordered, T_NEAR);
        mov(ecx, 2);
        mov(edx, 4);
        cmova(ecx, edx);
        mov(edx, 8);
        cmovb(ecx, edx);
        L(done);
        mov(edx, dword[rbx + m_layout.fpscr]);
        and_(edx, ~(0xFu << 12));
        mov(eax, ecx);
        shl(eax, 12);
        or_(edx, eax);
        mov(dword[rbx + m_layout.fpscr], edx);
        setCrField(field);
        cold([this, &unordered, &done, a, b, ordered] {
            L(unordered);
            // Signalling NaN: all-ones exponent, quiet bit clear, non-zero fraction. Shifted left
            // by one: strictly between 0xFFE0... and 0xFFF0....
            const auto signalling = [this](const Xbyak::Address &value) {
                mov(rax, value);
                shl(rax, 1);
                mov(rcx, 0xFFE0000000000000ull);
                cmp(rax, rcx);
                seta(dl);
                mov(rcx, 0xFFF0000000000000ull);
                cmp(rax, rcx);
                setb(cl);
                and_(dl, cl);
            };
            signalling(a);
            mov(r8b, dl);
            signalling(b);
            or_(dl, r8b);
            movzx(edx, dl);
            shl(edx, 24); // VXSNAN
            mov(eax, dword[rbx + m_layout.fpscr]);
            and_(eax, ~(1u << 24));
            or_(eax, edx);
            if (ordered)
                or_(eax, 1u << 19); // VXVC
            mov(dword[rbx + m_layout.fpscr], eax);
            mov(ecx, 1);
            jmp(done, T_NEAR);
        });
    }

    bool Emitter::emitFloatMemory(const EncodedInstruction &i, const InstructionID id)
    {
        bool isLoad = true, isDouble = false, indexed = false, update = false;
        switch (id) {
            case E_LFS:
                break;
            case E_LFSU:
                update = true;
                break;
            case E_LFSX:
                indexed = true;
                break;
            case E_LFSUX:
                indexed = update = true;
                break;
            case E_LFD:
                isDouble = true;
                break;
            case E_LFDU:
                isDouble = update = true;
                break;
            case E_LFDX:
                isDouble = indexed = true;
                break;
            case E_STFS:
                isLoad = false;
                break;
            case E_STFSU:
                isLoad = false;
                update = true;
                break;
            case E_STFSX:
                isLoad = false;
                indexed = true;
                break;
            case E_STFSUX:
                isLoad = false;
                indexed = update = true;
                break;
            case E_STFD:
                isLoad = false;
                isDouble = true;
                break;
            case E_STFDU:
                isLoad = false;
                isDouble = update = true;
                break;
            case E_STFDX:
                isLoad = false;
                isDouble = indexed = true;
                break;
            case E_STFIWX:
                // Low word of the FPR bits.
                effectiveAddress(i, true, false);
                mov(eax, dword[rbx + m_layout.fpr + static_cast<int>(i.frt * 8)]);
                store(4);
                return true;
            default:
                return false;
        }
        effectiveAddress(i, indexed, update);
        const auto finish = [this, ra = i.ra, update] {
            if (update)
                mov(gpr(ra), ecx);
        };
        if (isLoad) {
            if (isDouble) {
                load(8);
                mov(fpr(i.frt), rax);
            } else {
                load(4);
                vmovd(xmm0, eax);
                vcvtss2sd(xmm0, xmm0, xmm0);
                vmovsd(fpr(i.frt), xmm0);
                vmovsd(ps1(i.frt), xmm0);
            }
            finish();
            return true;
        }
        if (isDouble) {
            mov(rax, fpr(i.frt));
        } else {
            vmovsd(xmm0, fpr(i.frt));
            vcvtsd2ss(xmm0, xmm0, xmm0);
            vmovd(eax, xmm0);
        }
        if (update)
            store(isDouble ? 8 : 4, finish);
        else
            store(isDouble ? 8 : 4);
        return true;
    }

    bool Emitter::emitPaired(const EncodedInstruction &i, const InstructionID id)
    {
        if (!(m_layout.nativeMask & FamilyPaired))
            return false;
        const auto lane = [&](const Lane l, const unsigned r) { return l == Lane::Ps0 ? fpr(r) : ps1(r); };
        if (pairedArithmetic(id)) {
            // Both lanes are computed before either is written: FRT may be a source.
            arithmetic(id, fpr(i.fra), fpr(i.frb), fpr(i.frc));
            vmovapd(xmm4, xmm0);
            arithmetic(id, ps1(i.fra), ps1(i.frb), ps1(i.frc));
            Xbyak::Label &done = guardNaN({xmm4, xmm0});
            roundToSingle(xmm4);
            roundToSingle(xmm0);
            vmovsd(fpr(i.frt), xmm4);
            vmovsd(ps1(i.frt), xmm0);
            recordCr1(i);
            L(done);
            return true;
        }
        switch (id) {
            case E_PS_MULS0:
            case E_PS_MULS1:
            case E_PS_MADDS0:
            case E_PS_MADDS1: {
                // Both lanes scaled by one lane of FRC, read first.
                const bool fused = id == E_PS_MADDS0 || id == E_PS_MADDS1;
                vmovsd(xmm3, (id == E_PS_MULS0 || id == E_PS_MADDS0) ? fpr(i.frc) : ps1(i.frc));
                for (const Lane l: {Lane::Ps0, Lane::Ps1}) {
                    vmovsd(xmm0, lane(l, i.fra));
                    if (fused) {
                        vmovsd(xmm2, lane(l, i.frb));
                        vfmadd132sd(xmm0, xmm2, xmm3); // a * s + b
                    } else {
                        vmulsd(xmm0, xmm0, xmm3);
                    }
                    if (l == Lane::Ps0)
                        vmovapd(xmm4, xmm0);
                }
                Xbyak::Label &done = guardNaN({xmm4, xmm0});
                roundToSingle(xmm4);
                roundToSingle(xmm0);
                vmovsd(fpr(i.frt), xmm4);
                vmovsd(ps1(i.frt), xmm0);
                recordCr1(i);
                L(done);
                return true;
            }
            case E_PS_SUM0:
            case E_PS_SUM1:
                // sum0: ps0 = single(a.ps0 + b.ps1), ps1 = c.ps1; sum1: ps0 = c.ps0, ps1 = the sum.
                {
                    vmovsd(xmm0, fpr(i.fra));
                    vaddsd(xmm0, xmm0, ps1(i.frb));
                    Xbyak::Label &done = guardNaN({xmm0});
                    roundToSingle(xmm0);
                    if (id == E_PS_SUM0) {
                        mov(rax, ps1(i.frc));
                        mov(ps1(i.frt), rax);
                        vmovsd(fpr(i.frt), xmm0);
                    } else {
                        mov(rax, fpr(i.frc));
                        mov(fpr(i.frt), rax);
                        vmovsd(ps1(i.frt), xmm0);
                    }
                    recordCr1(i);
                    L(done);
                    return true;
                }
            case E_PS_SEL: {
                vxorpd(xmm1, xmm1, xmm1);
                for (const Lane l: {Lane::Ps0, Lane::Ps1}) {
                    Xbyak::Label keep;
                    vmovsd(xmm0, lane(l, i.fra));
                    mov(l == Lane::Ps0 ? r8 : r9, lane(l, i.frb));
                    vucomisd(xmm0, xmm1);
                    jb(keep);
                    mov(l == Lane::Ps0 ? r8 : r9, lane(l, i.frc));
                    L(keep);
                }
                mov(fpr(i.frt), r8);
                mov(ps1(i.frt), r9);
                recordCr1(i);
                return true;
            }
            case E_PS_MR:
            case E_PS_NEG:
            case E_PS_ABS:
            case E_PS_NABS:
                mov(r8, fpr(i.frb));
                mov(r9, ps1(i.frb));
                for (const auto &r: {r8, r9}) {
                    if (id == E_PS_NEG)
                        btc(r, 63);
                    else if (id == E_PS_ABS)
                        btr(r, 63);
                    else if (id == E_PS_NABS)
                        bts(r, 63);
                }
                mov(fpr(i.frt), r8);
                mov(ps1(i.frt), r9);
                recordCr1(i);
                return true;
            case E_PS_MERGE00:
            case E_PS_MERGE01:
            case E_PS_MERGE10:
            case E_PS_MERGE11:
                mov(r8, (id == E_PS_MERGE00 || id == E_PS_MERGE01) ? fpr(i.fra) : ps1(i.fra));
                mov(r9, (id == E_PS_MERGE00 || id == E_PS_MERGE10) ? fpr(i.frb) : ps1(i.frb));
                mov(fpr(i.frt), r8);
                mov(ps1(i.frt), r9);
                recordCr1(i);
                return true;
            case E_PS_CMPO0:
                compareFloat(fpr(i.fra), fpr(i.frb), i.bf, true);
                return true;
            case E_PSQ_L:
            case E_PSQ_ST:
                quantized(i, id);
                return true;
            default:
                return false;
        }
    }

    // psq_l / psq_st. Float GQR types (0-3) run inline; integer types go through the
    // interpreter's handler from cold code.
    void Emitter::quantized(const EncodedInstruction &i, const InstructionID id)
    {
        Xbyak::Label &integer = newLabel();
        Xbyak::Label &done = newLabel();
        const auto displacement = (i.psd & 0x800) ? static_cast<std::int32_t>(i.psd) - 0x1000 : static_cast<std::int32_t>(i.psd);
        // The type is bits 16-18 for loads, bits 0-2 for stores.
        mov(eax, dword[rbx + m_layout.gqr + static_cast<int>(i.psI * 4)]);
        test(eax, id == E_PSQ_L ? 0x40000u : 0x4u);
        jnz(integer, T_NEAR);
        if (i.ra) {
            mov(ecx, gpr(i.ra));
            if (displacement)
                add(ecx, displacement);
        } else {
            mov(ecx, static_cast<std::uint32_t>(displacement));
        }
        if (id == E_PSQ_L) {
            if (i.psW) {
                load(4);
                vmovd(xmm0, eax);
                mov(rax, kOne);
                vmovq(xmm1, rax);
            } else {
                // One 8-byte read: ps0 in the high word, ps1 in the low word.
                load(8);
                vmovd(xmm1, eax);
                shr(rax, 32);
                vmovd(xmm0, eax);
                vcvtss2sd(xmm1, xmm1, xmm1);
            }
            vcvtss2sd(xmm0, xmm0, xmm0);
            vmovsd(fpr(i.frt), xmm0);
            vmovsd(ps1(i.frt), xmm1);
        } else {
            // Lane by lane, as the interpreter writes them; a code write in the first lane
            // replays the whole instruction in the interpreter.
            vmovsd(xmm0, fpr(i.frt));
            vcvtsd2ss(xmm0, xmm0, xmm0);
            vmovd(eax, xmm0);
            store(4, {}, true);
            if (!i.psW) {
                add(ecx, 4);
                vmovsd(xmm0, ps1(i.frt));
                vcvtsd2ss(xmm0, xmm0, xmm0);
                vmovd(eax, xmm0);
                store(4);
            }
        }
        L(done);
        cold([this, &integer, &done, instr = m_current, pc = m_pc, index = m_index] {
            L(integer);
            calloutCall(instr, pc, index);
            jmp(done, T_NEAR);
        });
    }
} // namespace Core::Jit
