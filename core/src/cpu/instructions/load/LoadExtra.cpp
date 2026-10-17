/*
** EPITECH PROJECT, 2026
** core
** File description:
** LoadExtra -- integer loads with update / indexed / byte-reverse / reserve
*/

#include <bit>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief Load String Word Immediate.
     *        EA = (RA|0). NB (encoded in the RB slot, 0 means 32) bytes are loaded from EA into
     *        consecutive registers starting at RT, four bytes per register packed big-endian from
     *        the most significant byte; a partially filled last register is zero-padded on the
     *        right. The register index wraps from r31 to r0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt, ra, rb as NB).
     */
    void LSWI(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra];
        const std::uint32_t n = instr.rb == 0 ? 32 : instr.rb;
        std::uint32_t r = (instr.rt + 31u) % 32u; // pre-decrement so the first byte selects RT

        for (std::uint32_t i = 0; i < n; i++) {
            if (i % 4 == 0) {
                r = (r + 1) % 32u;
                cpu.m_gpr[r] = 0;
            }
            cpu.m_gpr[r] |= static_cast<std::uint32_t>(cpu.m_memory.read<std::uint8_t>(ea + i)) << (24 - 8 * (i % 4));
        }
    }

    /** @brief Load Halfword and Zero with Update. EA = RA + EXTS(D); RT = MEM(EA,2); RA = EA. */
    void LHZU(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(instr.d));

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint16_t>(ea);
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Load Halfword Algebraic with Update. EA = RA + EXTS(D); RT = EXTS(MEM(EA,2)); RA = EA. */
    void LHAU(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(instr.d));

        cpu.m_gpr[instr.rt] = static_cast<std::int16_t>(cpu.m_memory.read<std::uint16_t>(ea));
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Load Word and Zero with Update Indexed. EA = RA + RB; RT = MEM(EA,4); RA = EA. */
    void LWZUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint32_t>(ea);
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Load Byte and Zero with Update Indexed. EA = RA + RB; RT = MEM(EA,1); RA = EA. */
    void LBZUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint8_t>(ea);
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Load Halfword and Zero with Update Indexed. EA = RA + RB; RT = MEM(EA,2); RA = EA. */
    void LHZUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint16_t>(ea);
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Load Halfword Algebraic with Update Indexed. EA = RA + RB; RT = EXTS(MEM(EA,2)); RA = EA. */
    void LHAUX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = cpu.m_gpr[instr.ra] + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = static_cast<std::int16_t>(cpu.m_memory.read<std::uint16_t>(ea));
        cpu.m_gpr[instr.ra] = ea;
    }

    /** @brief Load Word Byte-Reverse Indexed. EA = (RA|0) + RB; RT = byte-reverse of MEM(EA,4). */
    void LWBRX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = std::byteswap(cpu.m_memory.read<std::uint32_t>(ea));
    }

    /**
     * @brief Load Word and Reserve Indexed. EA = (RA|0) + RB; RT = MEM(EA,4); set reservation.
     *        The reservation is cleared on every scheduler context switch, so an lwarx/stwcx.
     *        CAS that was interleaved with another thread fails and the guest retry loop
     *        re-reads — same guarantee the hardware reservation gives across cores.
     */
    void LWARX(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t ea = (instr.ra == 0 ? 0 : cpu.m_gpr[instr.ra]) + cpu.m_gpr[instr.rb];

        cpu.m_gpr[instr.rt] = cpu.m_memory.read<std::uint32_t>(ea);
        cpu.m_reserveValid = true;
        cpu.m_reserveAddr = ea;
    }

} // namespace Core::Instruction
