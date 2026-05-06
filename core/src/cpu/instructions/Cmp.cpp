#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // Write a 4-bit CR field value into the correct crfD slot.
    // Bit layout per field: [LT GT EQ SO] = bits [3 2 1 0]
    static void set_crf(Core::Interpreter &cpu, const EncodedInstruction &i, std::int64_t a, std::int64_t b)
    {
        const std::uint32_t crf = i.rt >> 2; // crfD = bits [25:23]
        const std::uint32_t val = (a < b ? 8u : 0u) | (a > b ? 4u : 0u) | (a == b ? 2u : 0u) | (cpu.m_xer.so ? 1u : 0u);

        const std::uint32_t shift = (7 - crf) * 4; // cr0 = bits [31:28], cr7 = bits [3:0]
        const std::uint32_t mask = 0xFu << shift;
        cpu.m_cr.raw = (cpu.m_cr.raw & ~mask) | (val << shift);
    }

    void CMP(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        set_crf(cpu, i, static_cast<std::int64_t>(cpu.m_gprSigned[i.ra]), static_cast<std::int64_t>(cpu.m_gprSigned[i.rb]));
    }

    void CMPL(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        set_crf(cpu, i, static_cast<std::int64_t>(cpu.m_gpr[i.ra]), static_cast<std::int64_t>(cpu.m_gpr[i.rb]));
    }

    void CMPI(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        set_crf(cpu, i, static_cast<std::int64_t>(cpu.m_gprSigned[i.ra]), static_cast<std::int64_t>(static_cast<std::int16_t>(i.si)));
    }

    void CMPLI(Core::Interpreter &cpu, const EncodedInstruction &i)
    {
        set_crf(cpu, i, static_cast<std::int64_t>(cpu.m_gpr[i.ra]), static_cast<std::int64_t>(static_cast<std::uint32_t>(i.si & 0xFFFF)));
    }

} // namespace Core::Instruction
