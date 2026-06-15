/*
** EPITECH PROJECT, 2025
** core
** File description:
** Branch
*/

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    static inline std::int32_t branchOffset(const EncodedInstruction &instr)
    {
        const auto value = static_cast<std::int32_t>(instr.li << 2);

        return (value << 6) >> 6;
    }

    /**
     * @brief Branch to target address (relative), without link.
     * @param cpu
     * @param instr
     */
    void B(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t offset = branchOffset(instr);

        if (instr.lk)
            cpu.m_lr = cpu.m_pc + 4;

        if (instr.aa)
            cpu.m_nextPc = static_cast<std::uint32_t>(offset) - Core::Memory::MemoryMap::ApplicationCode;
        else
            cpu.m_nextPc = cpu.m_pc + offset;
    }

    /**
     * @brief Branch to absolute target address, without link.
     * @param cpu
     * @param instr
     */
    void BA(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t offset = branchOffset(instr);

        cpu.m_nextPc = static_cast<std::uint32_t>(offset);
    }

    /**
     * @brief Branch to target address (relative) and store return address in LR.
     * @param cpu
     * @param instr
     */
    void BL(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t offset = branchOffset(instr);

        cpu.m_lr = cpu.m_pc + 4;
        cpu.m_nextPc = cpu.m_pc + offset;
    }

    /**
     * @brief Branch to absolute target address and store return address in LR.
     * @param cpu
     * @param instr
     */
    void BLA(Core::Interpreter &cpu, const EncodedInstruction &instr)
    {
        const std::int32_t offset = branchOffset(instr);

        cpu.m_lr = cpu.m_pc + 4;
        cpu.m_nextPc = static_cast<std::uint32_t>(offset);
    }
} // namespace Core::Instruction
