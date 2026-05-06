//
// Created by nicolas on 4/9/26.
//


#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {
    /**
     * @brief Extend Sign Halfword.
     *        RA = EXTS(RS[16:31]). Sign-extends the low 16 bits of RS to 32 bits.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rc).
     */
    void EXTSH(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = static_cast<std::uint32_t>(static_cast<std::int16_t>(cpu.m_gpr[instr.rt]));
        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }

    /**
     * @brief Extend Sign Byte.
     *        RA = EXTS(RS[24:31]). Sign-extends the low 8 bits of RS to 32 bits.
     *        If RC=1, updates CR0.
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: rt as RS, ra as RA dest, rc).
     */
    void EXTSB(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        cpu.m_gpr[instr.ra] = static_cast<std::uint32_t>(static_cast<std::int8_t>(cpu.m_gpr[instr.rt]));
        cpu.updateCR0(cpu.m_gprSigned[instr.ra], instr);
    }
} // namespace Core::Instruction
