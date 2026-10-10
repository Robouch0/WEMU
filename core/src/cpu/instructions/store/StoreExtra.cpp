/*
** EPITECH PROJECT, 2026
** core
** File description:
** StoreExtra -- integer stores with update / indexed / byte-reverse / conditional, dcbz
*/

#include <bit>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Store String Word Immediate.
     *        EA = (RA|0). NB (encoded in the RB slot, 0 means 32) bytes are stored to EA from
     *        consecutive registers starting at RS, four bytes per register taken big-endian from
     *        the most significant byte. The register index wraps from r31 to r0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rs, ra, rb as NB).
     */
    void STSWI(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra];
        const std::uint32_t n = instr.rb == 0 ? 32 : instr.rb;
        std::uint32_t r = (instr.rs + 31u) % 32u; // pre-decrement so the first byte selects RS

        for (std::uint32_t i = 0; i < n; i++) {
            if (i % 4 == 0)
                r = (r + 1) % 32u;
            cpu.m_memory.write<std::uint8_t>(ea + i, static_cast<std::uint8_t>(cpu.m_gpr[r] >> (24 - 8 * (i % 4))));
        }
    }

    /** @brief Store Byte with Update. EA = RA + EXTS(D); MEM(EA,1) = RS[24:31]; RA = EA. */
    void STBU(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(instr.d));

        cpu.m_memory.write<std::uint8_t>(ea, static_cast<std::uint8_t>(cpu.m_gpr[instr.rs]));
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Store Halfword with Update. EA = RA + EXTS(D); MEM(EA,2) = RS[16:31]; RA = EA. */
    void STHU(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(instr.d));

        cpu.m_memory.write<std::uint16_t>(ea, static_cast<std::uint16_t>(cpu.m_gpr[instr.rs]));
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Store Word with Update Indexed. EA = RA + RB; MEM(EA,4) = RS; RA = EA. */
    void STWUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_memory.write<std::uint32_t>(ea, cpu.m_gpr[instr.rs]);
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Store Byte with Update Indexed. EA = RA + RB; MEM(EA,1) = RS[24:31]; RA = EA. */
    void STBUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_memory.write<std::uint8_t>(ea, static_cast<std::uint8_t>(cpu.m_gpr[instr.rs]));
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Store Halfword with Update Indexed. EA = RA + RB; MEM(EA,2) = RS[16:31]; RA = EA. */
    void STHUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_memory.write<std::uint16_t>(ea, static_cast<std::uint16_t>(cpu.m_gpr[instr.rs]));
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Store Word Byte-Reverse Indexed. EA = (RA|0) + RB; MEM(EA,4) = byte-reverse of RS. */
    void STWBRX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb];

        cpu.m_memory.write<std::uint32_t>(ea, std::byteswap(cpu.m_gpr[instr.rs]));
    }

    /**
     * @brief Store Word Conditional Indexed (stwcx.). EA = (RA|0) + RB; MEM(EA,4) = RS if the
     *        lwarx reservation is still held (context switches clear it, so a CAS interleaved
     *        with another thread fails and the guest loop retries — the same guarantee the
     *        hardware reservation gives across cores). CR0.EQ reports success.
     */
    void STWCX_(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb];
        const bool reserved = cpu.m_reserveValid && cpu.m_reserveAddr == ea;

        if (reserved)
            cpu.m_memory.write<std::uint32_t>(ea, cpu.m_gpr[instr.rs]);
        cpu.m_reserveValid = false;
        cpu.m_cr.cr0 = (reserved ? Core::ConditionRegisterFlag::Zero : 0) | (cpu.m_xer.so ? Core::ConditionRegisterFlag::SummaryOverflow : 0);
    }

    /** @brief Data Cache Block set to Zero. Zeroes the 32-byte block containing EA = (RA|0) + RB. */
    void DCBZ(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = ((instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb]) & ~0x1Fu;

        for (std::uint32_t i = 0; i < 32; i += sizeof(std::uint32_t))
            cpu.m_memory.write<std::uint32_t>(ea + i, 0);
    }

} // namespace Core::Instruction
