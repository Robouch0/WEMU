/*
** EPITECH PROJECT, 2025
** core
** File description:
** Memory load/store instructions
*/

#include <bit>
#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // Compute effective address: EA = (RA==0 ? 0 : GPR[RA]) + sign_extend(SI)
    static inline std::uint32_t effectiveAddress(const Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t disp = static_cast<std::int16_t>(instr.si);
        const std::uint32_t base = (instr.ra != 0) ? cpu.m_gpr[instr.ra] : 0u;
        return base + static_cast<std::uint32_t>(disp);
    }

    // Translate virtual address to host pointer; returns nullptr if invalid
    static inline void *hostPtr(const Core::Interpreter &cpu, std::uint32_t ea)
    {
        const std::size_t host = cpu.m_binary.m_memory.translate(ea);
        if (host == 0)
            return nullptr;
        return reinterpret_cast<void *>(host);
    }

    // ─── Load instructions ──────────────────────────────────────────────────────

    // LWZ rt, d(ra) — load word and zero
    void LWZ(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            std::uint32_t raw;
            std::memcpy(&raw, ptr, 4);
            cpu.m_gpr[instr.rt] = std::byteswap(raw); // PowerPC is big-endian
        }
    }

    // LWZU rt, d(ra) — load word and zero with update
    void LWZU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            std::uint32_t raw;
            std::memcpy(&raw, ptr, 4);
            cpu.m_gpr[instr.rt] = std::byteswap(raw);
        }
        cpu.m_gpr[instr.ra] = ea;
    }

    // LBZ rt, d(ra) — load byte and zero
    void LBZ(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            std::uint8_t byte;
            std::memcpy(&byte, ptr, 1);
            cpu.m_gpr[instr.rt] = byte;
        }
    }

    // LBZU rt, d(ra) — load byte and zero with update
    void LBZU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            std::uint8_t byte;
            std::memcpy(&byte, ptr, 1);
            cpu.m_gpr[instr.rt] = byte;
        }
        cpu.m_gpr[instr.ra] = ea;
    }

    // LHZ rt, d(ra) — load halfword and zero
    void LHZ(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            std::uint16_t raw;
            std::memcpy(&raw, ptr, 2);
            cpu.m_gpr[instr.rt] = std::byteswap(raw);
        }
    }

    // ─── Store instructions ─────────────────────────────────────────────────────

    // STW rs, d(ra) — store word
    void STW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            const std::uint32_t val = std::byteswap(cpu.m_gpr[instr.rt]);
            std::memcpy(ptr, &val, 4);
        }
    }

    // STWU rs, d(ra) — store word with update
    void STWU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            const std::uint32_t val = std::byteswap(cpu.m_gpr[instr.rt]);
            std::memcpy(ptr, &val, 4);
        }
        cpu.m_gpr[instr.ra] = ea;
    }

    // STB rs, d(ra) — store byte
    void STB(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            const std::uint8_t val = static_cast<std::uint8_t>(cpu.m_gpr[instr.rt]);
            std::memcpy(ptr, &val, 1);
        }
    }

    // STBU rs, d(ra) — store byte with update
    void STBU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            const std::uint8_t val = static_cast<std::uint8_t>(cpu.m_gpr[instr.rt]);
            std::memcpy(ptr, &val, 1);
        }
        cpu.m_gpr[instr.ra] = ea;
    }

    // STH rs, d(ra) — store halfword
    void STH(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = effectiveAddress(cpu, instr);
        if (void *ptr = hostPtr(cpu, ea)) {
            const std::uint16_t val = std::byteswap(static_cast<std::uint16_t>(cpu.m_gpr[instr.rt]));
            std::memcpy(ptr, &val, 2);
        }
    }

} // namespace Core::Instruction
