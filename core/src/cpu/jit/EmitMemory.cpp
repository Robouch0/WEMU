/*
** EPITECH PROJECT, 2026
** core
** File description:
** EmitMemory -- integer loads and stores
*/

#include "Emitter.hpp"

namespace Core::Jit {
    namespace {
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

    bool Emitter::emitMemory(const EncodedInstruction &i, const InstructionID id)
    {
        Access a{};
        if ((m_layout.nativeMask & FamilyLoad) && lookupLoad(id, a)) {
            effectiveAddress(i, a.indexed, a.update);
            load(a.bytes, a.signExtend);
            if (a.byteReverse)
                bswap(eax);
            mov(gpr(i.rt), eax);
            if (a.update)
                mov(gpr(i.ra), ecx);
            return true;
        }
        if ((m_layout.nativeMask & FamilyStore) && lookupStore(id, a)) {
            effectiveAddress(i, a.indexed, a.update);
            mov(eax, gpr(i.rs)); // read before the update: RS may be RA
            if (a.byteReverse)
                bswap(eax);
            if (a.update)
                store(a.bytes, [this, ra = i.ra] { mov(gpr(ra), ecx); });
            else
                store(a.bytes);
            return true;
        }
        return false;
    }
} // namespace Core::Jit
