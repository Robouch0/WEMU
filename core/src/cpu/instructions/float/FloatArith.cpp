/*
** EPITECH PROJECT, 2026
** core
** File description:
** FloatArith -- scalar floating-point arithmetic (single + double), conversions, select
*/

#include <cfenv>
#include <cmath>
#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    // Round a double result to single precision (the storage format is still double in the FPR).
    static inline double toSingle(const double v) { return static_cast<double>(static_cast<float>(v)); }

    static inline void storeSingleResult(Interpreter &cpu, const EncodedInstruction &i, const double value)
    {
        // Espresso single-precision results populate both paired-single lanes.
        cpu.m_fpr[i.frt] = toSingle(value);
        cpu.m_ps1[i.frt] = cpu.m_fpr[i.frt];
        cpu.updateCR1(i);
    }

    static inline std::int32_t toInt32Clamped(const double v)
    {
        if (std::isnan(v))
            return static_cast<std::int32_t>(0x80000000);
        if (v >= 2147483647.0)
            return 0x7FFFFFFF;
        if (v <= -2147483648.0)
            return static_cast<std::int32_t>(0x80000000);
        return static_cast<std::int32_t>(v);
    }

    static inline void storeIntInFpr(Interpreter &cpu, const std::uint32_t frt, const std::int32_t value)
    {
        // fctiw* place the 32-bit integer in the low word of the FPR; only stfiwx reads it back.
        const std::uint64_t bits = 0xFFF8000000000000ull | static_cast<std::uint32_t>(value);
        std::memcpy(&cpu.m_fpr[frt], &bits, sizeof(double));
    }

    // ---- Double-precision arithmetic (op63 A-form) ----
    void FADD(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = cpu.m_fpr[i.fra] + cpu.m_fpr[i.frb]; cpu.updateCR1(i); }
    void FSUB(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = cpu.m_fpr[i.fra] - cpu.m_fpr[i.frb]; cpu.updateCR1(i); }
    void FMUL(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = cpu.m_fpr[i.fra] * cpu.m_fpr[i.frc]; cpu.updateCR1(i); }
    void FDIV(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = cpu.m_fpr[i.fra] / cpu.m_fpr[i.frb]; cpu.updateCR1(i); }
    void FSQRT(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = std::sqrt(cpu.m_fpr[i.frb]); cpu.updateCR1(i); }
    void FRSQRTE(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = 1.0 / std::sqrt(cpu.m_fpr[i.frb]); cpu.updateCR1(i); }
    void FMADD(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], cpu.m_fpr[i.frb]); cpu.updateCR1(i); }
    void FMSUB(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], -cpu.m_fpr[i.frb]); cpu.updateCR1(i); }
    void FNMADD(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = -std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], cpu.m_fpr[i.frb]); cpu.updateCR1(i); }
    void FNMSUB(Interpreter &cpu, const EncodedInstruction &i) { cpu.m_fpr[i.frt] = -std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], -cpu.m_fpr[i.frb]); cpu.updateCR1(i); }

    /** @brief Floating Select. FRT = (FRA >= 0.0 && !NaN) ? FRC : FRB. */
    void FSEL(Interpreter &cpu, const EncodedInstruction &i)
    {
        const double a = cpu.m_fpr[i.fra];
        cpu.m_fpr[i.frt] = (a >= 0.0 && !std::isnan(a)) ? cpu.m_fpr[i.frc] : cpu.m_fpr[i.frb];
        cpu.updateCR1(i);
    }

    // ---- Single-precision arithmetic (op59 A-form) ----
    void FADDS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, cpu.m_fpr[i.fra] + cpu.m_fpr[i.frb]); }
    void FSUBS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, cpu.m_fpr[i.fra] - cpu.m_fpr[i.frb]); }
    void FMULS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, cpu.m_fpr[i.fra] * cpu.m_fpr[i.frc]); }
    void FDIVS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, cpu.m_fpr[i.fra] / cpu.m_fpr[i.frb]); }
    void FRES(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, 1.0 / cpu.m_fpr[i.frb]); }
    void FMADDS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], cpu.m_fpr[i.frb])); }
    void FMSUBS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], -cpu.m_fpr[i.frb])); }
    void FNMADDS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, -std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], cpu.m_fpr[i.frb])); }
    void FNMSUBS(Interpreter &cpu, const EncodedInstruction &i) { storeSingleResult(cpu, i, -std::fma(cpu.m_fpr[i.fra], cpu.m_fpr[i.frc], -cpu.m_fpr[i.frb])); }

    // ---- Round / convert (op63 X-form) ----
    /** @brief Round to Single-Precision. FRT = RoundToSingle(FRB). */
    void FRSP(Interpreter &cpu, const EncodedInstruction &i)
    {
        storeSingleResult(cpu, i, cpu.m_fpr[i.frb]);
    }

    /** @brief Floating Negative Absolute Value. FRT = -|FRB|. */
    void FNABS(Interpreter &cpu, const EncodedInstruction &i)
    {
        cpu.m_fpr[i.frt] = -std::fabs(cpu.m_fpr[i.frb]);
        cpu.updateCR1(i);
    }

    /** @brief Convert to Integer Word (round per FPSCR[RN]; we use round-to-nearest). */
    void FCTIW(Interpreter &cpu, const EncodedInstruction &i)
    {
        storeIntInFpr(cpu, i.frt, toInt32Clamped(std::nearbyint(cpu.m_fpr[i.frb])));
        cpu.updateCR1(i);
    }

    /** @brief Convert to Integer Word with round toward Zero. */
    void FCTIWZ(Interpreter &cpu, const EncodedInstruction &i)
    {
        storeIntInFpr(cpu, i.frt, toInt32Clamped(std::trunc(cpu.m_fpr[i.frb])));
        cpu.updateCR1(i);
    }

} // namespace Core::Instruction
