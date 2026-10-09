/*
** EPITECH PROJECT, 2026
** core
** File description:
** Trap -- tw (trap word)
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "utils/Logger.hpp"

namespace Core::Instruction {

    /**
     * @brief Trap Word. Compares RA and RB; if any condition selected by the TO field (bits 6-10)
     *        holds, a trap is generated. Real hardware would raise an exception; here we log it and
     *        continue so a single assertion does not abort the whole boot.
     *        TO bits: 0x10 LT, 0x08 GT, 0x04 EQ (signed), 0x02 LTU, 0x01 GTU (unsigned).
     */
    void TW(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint32_t to = instr.rt; // TO field occupies the RT slot (bits 6-10)
        const std::int32_t a = cpu.m_gprSigned[instr.ra];
        const std::int32_t b = cpu.m_gprSigned[instr.rb];
        const std::uint32_t ua = cpu.m_gpr[instr.ra];
        const std::uint32_t ub = cpu.m_gpr[instr.rb];

        const bool trapped = ((to & 0x10) && a < b) || ((to & 0x08) && a > b) || ((to & 0x04) && a == b) || ((to & 0x02) && ua < ub)
            || ((to & 0x01) && ua > ub);

        if (trapped)
            Utils::Log::warn("[CPU] tw trap (TO=0x{:X}, ra=0x{:08X}, rb=0x{:08X})", to, ua, ub);
    }

} // namespace Core::Instruction
