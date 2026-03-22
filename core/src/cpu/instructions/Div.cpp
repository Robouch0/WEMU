/*
** EPITECH PROJECT, 2025
** core
** File description:
** Div
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    /**
     * @brief The quotient (RA) / (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void DIVW(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int64_t ra = cpu.m_gprSigned[instr.ra];
        const std::int64_t rb = cpu.m_gprSigned[instr.rb];

        if (instr.oe) {
            cpu.m_xer.ov = (ra == static_cast<std::int32_t>(0x80000000) && rb == -1) || (rb == 0);

            if (cpu.m_xer.ov) {
                cpu.m_xer.so = true;
                if (instr.rc)
                    cpu.m_cr.cr0.so = true;
                return;
            }
        }

        const std::int64_t result = ra / rb;
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);

        if (instr.rc) {
            cpu.m_cr.cr0.lt = result < 0;
            cpu.m_cr.cr0.gt = result > 0;
            cpu.m_cr.cr0.eq = result == 0;
        }
    }

    /**
     * @brief The quotient (RA) / (RB) is placed into register RT.
     * @param cpu
     * @param instr
     */
    void DIVWU(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::uint64_t ra = cpu.m_gpr[instr.ra];
        const std::uint64_t rb = cpu.m_gpr[instr.rb];

        if (instr.oe) {
            cpu.m_xer.ov = rb == 0;

            if (cpu.m_xer.ov) {
                cpu.m_xer.so = true;
                if (instr.rc)
                    cpu.m_cr.cr0.so = true;
                return;
            }
        }

        const std::uint64_t result = ra / rb;
        cpu.m_gpr[instr.rt] = static_cast<std::uint32_t>(result);

        if (instr.rc) {
            const std::int32_t signedRt = cpu.m_gprSigned[instr.rt];
            cpu.m_cr.cr0.lt = signedRt < 0;
            cpu.m_cr.cr0.gt = signedRt > 0;
            cpu.m_cr.cr0.eq = signedRt == 0;
        }
    }
};