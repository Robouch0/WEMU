/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatLoadStoreExtra -- fp loads/stores with update / indexed, stfiwx
*/

#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    static inline double loadSingle(Interpreter &cpu, const std::uint32_t ea)
    {
        const std::uint32_t raw = cpu.m_memory.read<std::uint32_t>(ea);
        float f;
        std::memcpy(&f, &raw, sizeof(f));
        return static_cast<double>(f);
    }

    static inline double loadDouble(Interpreter &cpu, const std::uint32_t ea)
    {
        const std::uint64_t raw = cpu.m_memory.read<std::uint64_t>(ea);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    static inline void storeSingle(Interpreter &cpu, const std::uint32_t ea, const double value)
    {
        const auto f = static_cast<float>(value);
        std::uint32_t raw;
        std::memcpy(&raw, &f, sizeof(raw));
        cpu.m_memory.write<std::uint32_t>(ea, raw);
    }

    static inline void storeDouble(Interpreter &cpu, const std::uint32_t ea, const double value)
    {
        std::uint64_t raw;
        std::memcpy(&raw, &value, sizeof(raw));
        cpu.m_memory.write<std::uint64_t>(ea, raw);
    }

    static inline std::uint32_t dform(Interpreter &cpu, const EncodedInstruction &i)
    {
        return (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + static_cast<std::uint32_t>(static_cast<std::int16_t>(i.d));
    }

    // ---- Loads ----
    void LFSU(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = cpu.m_gpr[i.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(i.d));
        cpu.m_fpr[i.frt] = loadSingle(cpu, ea);
        cpu.m_ps1[i.frt] = cpu.m_fpr[i.frt];
        cpu.m_gpr[i.ra] = ea;
    }
    void LFDU(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = cpu.m_gpr[i.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(i.d));
        cpu.m_fpr[i.frt] = loadDouble(cpu, ea);
        cpu.m_gpr[i.ra] = ea;
    }
    void LFSX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + cpu.m_gpr[i.rb];
        cpu.m_fpr[i.frt] = loadSingle(cpu, ea);
        cpu.m_ps1[i.frt] = cpu.m_fpr[i.frt];
    }
    void LFSUX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = cpu.m_gpr[i.ra] + cpu.m_gpr[i.rb];
        cpu.m_fpr[i.frt] = loadSingle(cpu, ea);
        cpu.m_ps1[i.frt] = cpu.m_fpr[i.frt];
        cpu.m_gpr[i.ra] = ea;
    }
    void LFDX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + cpu.m_gpr[i.rb];
        cpu.m_fpr[i.frt] = loadDouble(cpu, ea);
    }

    // ---- Stores ----
    void STFS(Interpreter &cpu, const EncodedInstruction &i) { storeSingle(cpu, dform(cpu, i), cpu.m_fpr[i.frt]); }
    void STFD(Interpreter &cpu, const EncodedInstruction &i) { storeDouble(cpu, dform(cpu, i), cpu.m_fpr[i.frt]); }
    void STFSU(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = cpu.m_gpr[i.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(i.d));
        storeSingle(cpu, ea, cpu.m_fpr[i.frt]);
        cpu.m_gpr[i.ra] = ea;
    }
    void STFDU(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = cpu.m_gpr[i.ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(i.d));
        storeDouble(cpu, ea, cpu.m_fpr[i.frt]);
        cpu.m_gpr[i.ra] = ea;
    }
    void STFSX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + cpu.m_gpr[i.rb];
        storeSingle(cpu, ea, cpu.m_fpr[i.frt]);
    }
    void STFSUX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = cpu.m_gpr[i.ra] + cpu.m_gpr[i.rb];
        storeSingle(cpu, ea, cpu.m_fpr[i.frt]);
        cpu.m_gpr[i.ra] = ea;
    }
    void STFDX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + cpu.m_gpr[i.rb];
        storeDouble(cpu, ea, cpu.m_fpr[i.frt]);
    }

    /** @brief Store Floating-Point as Integer Word Indexed. MEM(EA,4) = low 32 bits of FRS. */
    void STFIWX(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + cpu.m_gpr[i.rb];
        std::uint64_t bits;
        std::memcpy(&bits, &cpu.m_fpr[i.frt], sizeof(bits));
        cpu.m_memory.write<std::uint32_t>(ea, static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
    }

} // namespace Core::Instruction
