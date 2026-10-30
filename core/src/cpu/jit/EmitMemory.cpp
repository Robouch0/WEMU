/*
** EPITECH PROJECT, 2026
** core
** File description:
** EmitMemory -- integer loads and stores
*/

#include <cstddef>

#include "Emitter.hpp"
#include "cpu/memory/Memory.hpp"

namespace Core::Jit {
    namespace {
        constexpr std::int32_t minus(const std::uint32_t base) { return static_cast<std::int32_t>(0u - base); }

        struct Access {
                unsigned bytes;
                bool indexed, update, signExtend = false, byteReverse = false;
        };

        constexpr bool lookupLoad(const InstructionID id, Access &a)
        {
            switch (id) {
                case E_LBZ:
                    a = {1, false, false};
                    return true;
                case E_LBZU:
                    a = {1, false, true};
                    return true;
                case E_LBZX:
                    a = {1, true, false};
                    return true;
                case E_LBZUX:
                    a = {1, true, true};
                    return true;
                case E_LHZ:
                    a = {2, false, false};
                    return true;
                case E_LHZU:
                    a = {2, false, true};
                    return true;
                case E_LHZX:
                    a = {2, true, false};
                    return true;
                case E_LHZUX:
                    a = {2, true, true};
                    return true;
                case E_LHA:
                    a = {2, false, false, true};
                    return true;
                case E_LHAU:
                    a = {2, false, true, true};
                    return true;
                case E_LHAX:
                    a = {2, true, false, true};
                    return true;
                case E_LHAUX:
                    a = {2, true, true, true};
                    return true;
                case E_LWZ:
                    a = {4, false, false};
                    return true;
                case E_LWZU:
                    a = {4, false, true};
                    return true;
                case E_LWZX:
                    a = {4, true, false};
                    return true;
                case E_LWZUX:
                    a = {4, true, true};
                    return true;
                case E_LWBRX:
                    a = {4, true, false, false, true};
                    return true;
                case E_LHBRX:
                    a = {2, true, false, false, true};
                    return true;
                default:
                    return false;
            }
        }

        constexpr bool lookupStore(const InstructionID id, Access &a)
        {
            switch (id) {
                case E_STB:
                    a = {1, false, false};
                    return true;
                case E_STBU:
                    a = {1, false, true};
                    return true;
                case E_STBX:
                    a = {1, true, false};
                    return true;
                case E_STBUX:
                    a = {1, true, true};
                    return true;
                case E_STH:
                    a = {2, false, false};
                    return true;
                case E_STHU:
                    a = {2, false, true};
                    return true;
                case E_STHX:
                    a = {2, true, false};
                    return true;
                case E_STHUX:
                    a = {2, true, true};
                    return true;
                case E_STW:
                    a = {4, false, false};
                    return true;
                case E_STWU:
                    a = {4, false, true};
                    return true;
                case E_STWX:
                    a = {4, true, false};
                    return true;
                case E_STWUX:
                    a = {4, true, true};
                    return true;
                case E_STWBRX:
                    a = {4, true, false, false, true};
                    return true;
                case E_STHBRX:
                    a = {2, true, false, false, true};
                    return true;
                default:
                    return false;
            }
        }
    } // namespace

    void Emitter::effectiveAddress(const EncodedInstruction &i, const bool indexed, const bool update)
    {
        // Update forms always add RA, even RA=0, as the interpreter does.
        if (indexed) {
            mov(ecx, gpr(i.rb));
            if (update || i.ra)
                add(ecx, gpr(i.ra));
            return;
        }
        const auto displacement = static_cast<std::int32_t>(static_cast<std::int16_t>(i.d));
        if (update || i.ra) {
            mov(ecx, gpr(i.ra));
            if (displacement)
                add(ecx, displacement);
        } else {
            mov(ecx, static_cast<std::uint32_t>(displacement));
        }
    }

    // lmw / stmw: registers RT..r31 from or to consecutive words. A range wholly in main RAM (and,
    // for stores, outside code and watched pages) or on the stack moves inline; anything else runs
    // the interpreter's handler, which also covers faults part-way through.
    void Emitter::multiple(const EncodedInstruction &i, const bool isStore)
    {
        const unsigned first = i.rt;
        const unsigned bytes = (32 - first) * 4;
        Xbyak::Label &body = newLabel();
        Xbyak::Label &stack = newLabel();
        Xbyak::Label &handler = newLabel();
        Xbyak::Label &done = newLabel();
        effectiveAddress(i, false, false);
        lea(edx, ptr[rcx + minus(Memory::ApplicationCode)]);
        cmp(edx, m_layout.ramBytes - bytes);
        ja(stack, T_NEAR);
        if (isStore) {
            // At most 128 bytes: the first and last pages cover the range.
            for (const unsigned end: {0u, bytes - 1}) {
                lea(r8d, ptr[rdx + end]);
                shr(r8d, Memory::kCodePageShift);
                mov(r9d, r8d);
                shr(r9d, 6);
                mov(r9, qword[r14 + r9 * 8]);
                bt(r9, r8);
                jc(handler, T_NEAR);
            }
        }
        lea(r8, ptr[r12 + rcx]);
        L(body);
        for (unsigned r = first, offset = 0; r < 32; ++r, offset += 4) {
            if (isStore) {
                mov(eax, gpr(r));
                movbe(dword[r8 + offset], eax);
            } else {
                movbe(eax, dword[r8 + offset]);
                mov(gpr(r), eax);
            }
        }
        L(done);
        cold([this, &stack, &body, &handler, &done, bytes, instr = m_current, pc = m_pc, index = m_index] {
            L(stack);
            lea(edx, ptr[rcx + minus(Memory::STACK_BASE)]);
            cmp(edx, Memory::STACK_SIZE - bytes);
            ja(handler, T_NEAR);
            mov(r8, qword[r15 + offsetof(Context, stackHost)]);
            add(r8, rdx);
            jmp(body, T_NEAR);
            L(handler);
            calloutCall(instr, pc, index);
            jmp(done, T_NEAR);
        });
    }

    bool Emitter::emitMemory(const EncodedInstruction &i, const InstructionID id)
    {
        Access a{};
        if ((m_layout.nativeMask & FamilyLoad) && lookupLoad(id, a)) {
            effectiveAddress(i, a.indexed, a.update);
            load(a.bytes, a.signExtend);
            if (a.byteReverse) {
                if (a.bytes == 2)
                    ror(ax, 8);
                else
                    bswap(eax);
            }
            mov(gpr(i.rt), eax);
            if (a.update)
                mov(gpr(i.ra), ecx);
            return true;
        }
        if ((id == E_LMW && (m_layout.nativeMask & FamilyLoad)) || (id == E_STMW && (m_layout.nativeMask & FamilyStore))) {
            multiple(i, id == E_STMW);
            return true;
        }
        if ((m_layout.nativeMask & FamilyStore) && lookupStore(id, a)) {
            effectiveAddress(i, a.indexed, a.update);
            mov(eax, gpr(i.rs)); // read before the update: RS may be RA
            if (a.byteReverse) {
                if (a.bytes == 2)
                    ror(ax, 8);
                else
                    bswap(eax);
            }
            if (a.update)
                store(a.bytes, [this, ra = i.ra] { mov(gpr(ra), ecx); });
            else
                store(a.bytes);
            return true;
        }
        return false;
    }
} // namespace Core::Jit
