/*
** EPITECH PROJECT, 2026
** core
** File description:
** CrLogical -- condition-register logical ops (op19) and mcrf
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // PPC numbers CR bits with the MSB as bit 0, so CR bit i lives at raw bit (31 - i).
    static inline std::uint32_t getCrBit(const std::uint32_t raw, const std::uint32_t i) { return (raw >> (31 - i)) & 1u; }

    static inline void setCrBit(std::uint32_t &raw, const std::uint32_t i, const std::uint32_t v)
    {
        const std::uint32_t mask = 1u << (31 - i);
        raw = v ? (raw | mask) : (raw & ~mask);
    }

    // BT = bits 6-10, BA = bits 11-15, BB = bits 16-20.
    template<typename Op>
    static inline void crLogical(Interpreter &cpu, const EncodedInstruction &instr, Op op)
    {
        const std::uint32_t a = getCrBit(cpu.m_cr.raw, instr.ra);
        const std::uint32_t b = getCrBit(cpu.m_cr.raw, instr.rb);
        setCrBit(cpu.m_cr.raw, instr.rt, op(a, b) & 1u);
    }

    void CRAND(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return a & b; }); }
    void CROR(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return a | b; }); }
    void CRXOR(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return a ^ b; }); }
    void CRNAND(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return ~(a & b); }); }
    void CRNOR(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return ~(a | b); }); }
    void CRANDC(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return a & ~b; }); }
    void CRORC(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return a | ~b; }); }
    void CREQV(Interpreter &cpu, const EncodedInstruction &instr) { crLogical(cpu, instr, [](auto a, auto b) { return ~(a ^ b); }); }

    /** @brief Move Condition Register Field. CR[BF] = CR[BFA]. BF = bits 6-8, BFA = bits 11-13. */
    void MCRF(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t bf = (instr.raw >> 23) & 0x7u;
        const std::uint32_t bfa = (instr.raw >> 18) & 0x7u;
        const std::uint32_t field = (cpu.m_cr.raw >> ((7 - bfa) * 4)) & 0xFu;
        const std::uint32_t shift = (7 - bf) * 4;

        cpu.m_cr.raw = (cpu.m_cr.raw & ~(0xFu << shift)) | (field << shift);
    }

} // namespace Core::Instruction
