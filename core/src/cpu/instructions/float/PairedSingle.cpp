/*
** EPITECH PROJECT, 2026
** core
** File description:
** PairedSingle -- Espresso paired-single ops (op4) and quantized load/store (psq_l/psq_st)
**
** Each FPR holds two single-precision lanes: ps0 (m_fpr) and ps1 (m_ps1). Paired-single ops
** operate on both lanes. Quantized load/store use a GQR (m_gqr[I]) to pick a data type and a
** signed 6-bit scale exponent; the common case (GQR=0) is plain float with no scaling.
*/

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Instruction {

    static inline double single(const double v) { return static_cast<double>(static_cast<float>(v)); }

    // Apply a paired op lane-wise then round both lanes to single precision.
    template<typename F>
    static inline void pairLanes(Interpreter &cpu, const EncodedInstruction &i, F op)
    {
        const double r0 = op(cpu.m_fpr[i.fra], cpu.m_fpr[i.frb], cpu.m_fpr[i.frc]);
        const double r1 = op(cpu.m_ps1[i.fra], cpu.m_ps1[i.frb], cpu.m_ps1[i.frc]);
        cpu.m_fpr[i.frt] = single(r0);
        cpu.m_ps1[i.frt] = single(r1);
        cpu.updateCR1(i);
    }

    void PS_ADD(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double) { return a + b; });
    }
    void PS_SUB(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double) { return a - b; });
    }
    void PS_MUL(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double, double cc) { return a * cc; });
    }
    void PS_DIV(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double) { return a / b; });
    }
    void PS_MADD(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double cc) { return std::fma(a, cc, b); });
    }
    void PS_MSUB(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double cc) { return std::fma(a, cc, -b); });
    }
    void PS_NMADD(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double cc) { return -std::fma(a, cc, b); });
    }
    void PS_NMSUB(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double a, double b, double cc) { return -std::fma(a, cc, -b); });
    }
    void PS_RES(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double, double b, double) { return 1.0 / b; });
    }
    void PS_RSQRTE(Interpreter &c, const EncodedInstruction &i)
    {
        pairLanes(c, i, [](double, double b, double) { return 1.0 / std::sqrt(b); });
    }

    // muls* multiply both lanes by a single broadcast lane of FRC.
    void PS_MULS0(Interpreter &c, const EncodedInstruction &i)
    {
        const double s = c.m_fpr[i.frc];
        c.m_fpr[i.frt] = single(c.m_fpr[i.fra] * s);
        c.m_ps1[i.frt] = single(c.m_ps1[i.fra] * s);
        c.updateCR1(i);
    }
    void PS_MULS1(Interpreter &c, const EncodedInstruction &i)
    {
        const double s = c.m_ps1[i.frc];
        c.m_fpr[i.frt] = single(c.m_fpr[i.fra] * s);
        c.m_ps1[i.frt] = single(c.m_ps1[i.fra] * s);
        c.updateCR1(i);
    }
    void PS_MADDS0(Interpreter &c, const EncodedInstruction &i)
    {
        const double s = c.m_fpr[i.frc];
        c.m_fpr[i.frt] = single(std::fma(c.m_fpr[i.fra], s, c.m_fpr[i.frb]));
        c.m_ps1[i.frt] = single(std::fma(c.m_ps1[i.fra], s, c.m_ps1[i.frb]));
        c.updateCR1(i);
    }
    void PS_MADDS1(Interpreter &c, const EncodedInstruction &i)
    {
        const double s = c.m_ps1[i.frc];
        c.m_fpr[i.frt] = single(std::fma(c.m_fpr[i.fra], s, c.m_fpr[i.frb]));
        c.m_ps1[i.frt] = single(std::fma(c.m_ps1[i.fra], s, c.m_ps1[i.frb]));
        c.updateCR1(i);
    }

    // sum0: ps0 = a.ps0 + b.ps1, ps1 = c.ps1 ; sum1: ps0 = c.ps0, ps1 = a.ps0 + b.ps1
    void PS_SUM0(Interpreter &c, const EncodedInstruction &i)
    {
        const double r = single(c.m_fpr[i.fra] + c.m_ps1[i.frb]);
        c.m_ps1[i.frt] = c.m_ps1[i.frc];
        c.m_fpr[i.frt] = r;
        c.updateCR1(i);
    }
    void PS_SUM1(Interpreter &c, const EncodedInstruction &i)
    {
        const double r = single(c.m_fpr[i.fra] + c.m_ps1[i.frb]);
        c.m_fpr[i.frt] = c.m_fpr[i.frc];
        c.m_ps1[i.frt] = r;
        c.updateCR1(i);
    }

    void PS_SEL(Interpreter &c, const EncodedInstruction &i)
    {
        const double a0 = c.m_fpr[i.fra];
        const double a1 = c.m_ps1[i.fra];
        c.m_fpr[i.frt] = (a0 >= 0.0 && !std::isnan(a0)) ? c.m_fpr[i.frc] : c.m_fpr[i.frb];
        c.m_ps1[i.frt] = (a1 >= 0.0 && !std::isnan(a1)) ? c.m_ps1[i.frc] : c.m_ps1[i.frb];
        c.updateCR1(i);
    }

    // ---- Moves / merges (X-form) ----
    void PS_MR(Interpreter &c, const EncodedInstruction &i)
    {
        c.m_fpr[i.frt] = c.m_fpr[i.frb];
        c.m_ps1[i.frt] = c.m_ps1[i.frb];
        c.updateCR1(i);
    }
    void PS_NEG(Interpreter &c, const EncodedInstruction &i)
    {
        c.m_fpr[i.frt] = -c.m_fpr[i.frb];
        c.m_ps1[i.frt] = -c.m_ps1[i.frb];
        c.updateCR1(i);
    }
    void PS_ABS(Interpreter &c, const EncodedInstruction &i)
    {
        c.m_fpr[i.frt] = std::fabs(c.m_fpr[i.frb]);
        c.m_ps1[i.frt] = std::fabs(c.m_ps1[i.frb]);
        c.updateCR1(i);
    }
    void PS_NABS(Interpreter &c, const EncodedInstruction &i)
    {
        c.m_fpr[i.frt] = -std::fabs(c.m_fpr[i.frb]);
        c.m_ps1[i.frt] = -std::fabs(c.m_ps1[i.frb]);
        c.updateCR1(i);
    }

    void PS_MERGE00(Interpreter &c, const EncodedInstruction &i)
    {
        const double a0 = c.m_fpr[i.fra];
        const double b0 = c.m_fpr[i.frb];
        c.m_fpr[i.frt] = a0;
        c.m_ps1[i.frt] = b0;
        c.updateCR1(i);
    }

    void PS_MERGE01(Interpreter &c, const EncodedInstruction &i)
    {
        const double a0 = c.m_fpr[i.fra];
        const double b1 = c.m_ps1[i.frb];
        c.m_fpr[i.frt] = a0;
        c.m_ps1[i.frt] = b1;
        c.updateCR1(i);
    }

    void PS_MERGE10(Interpreter &c, const EncodedInstruction &i)
    {
        const double a1 = c.m_ps1[i.fra];
        const double b0 = c.m_fpr[i.frb];
        c.m_fpr[i.frt] = a1;
        c.m_ps1[i.frt] = b0;
        c.updateCR1(i);
    }

    void PS_MERGE11(Interpreter &c, const EncodedInstruction &i)
    {
        const double a1 = c.m_ps1[i.fra];
        const double b1 = c.m_ps1[i.frb];
        c.m_fpr[i.frt] = a1;
        c.m_ps1[i.frt] = b1;
        c.updateCR1(i);
    }

    // ---- Compare ----

    static void setCRField(Core::ConditionRegister &cr, const std::uint32_t field, const std::uint32_t value)
    {
        switch (field & 0x7u) {
            case 0:
                cr.cr0 = value;
                break;
            case 1:
                cr.cr1 = value;
                break;
            case 2:
                cr.cr2 = value;
                break;
            case 3:
                cr.cr3 = value;
                break;
            case 4:
                cr.cr4 = value;
                break;
            case 5:
                cr.cr5 = value;
                break;
            case 6:
                cr.cr6 = value;
                break;
            default:
                cr.cr7 = value;
                break;
        }
    }

    /**
     * @brief Paired Single Compare Ordered (ps0 lane).
     *        Compares FRA.ps0 and FRB.ps0, records LT/GT/EQ/UN in CR field BF and FPSCR[FPCC].
     *        Ordered compare: a NaN operand also raises FPSCR[VXVC] (and VXSNAN if signaling).
     * @param cpu   Interpreter state.
     * @param instr Encoded instruction (fields: fra, frb; BF at raw[23:25]).
     */
    void PS_CMPO0(Interpreter &cpu, const EncodedInstruction &instr)
    {
        const double a = cpu.m_fpr[instr.fra];
        const double b = cpu.m_fpr[instr.frb];
        std::uint32_t condition = Core::ConditionRegisterFlag::Zero;

        if (std::isnan(a) || std::isnan(b)) {
            condition = Core::ConditionRegisterFlag::SummaryOverflow;
            const auto isSNaN = [](const double v) {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(v);
                return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (bits & 0x000FFFFFFFFFFFFFull) != 0 &&
                       (bits & 0x0008000000000000ull) == 0;
            };
            cpu.m_fpscr.vxsnan = isSNaN(a) || isSNaN(b);
            cpu.m_fpscr.vxvc = true;
        } else if (a < b) {
            condition = Core::ConditionRegisterFlag::Negative;
        } else if (a > b) {
            condition = Core::ConditionRegisterFlag::Positive;
        }

        cpu.m_fpscr.fpcc = condition;
        setCRField(cpu.m_cr, instr.bf, condition);
    }

    // ---- Quantized load/store ----
    static inline int scale6(std::uint32_t s) { return (s & 0x20) ? static_cast<int>(s) - 64 : static_cast<int>(s); }

    static double dequant(Interpreter &cpu, std::uint32_t ea, std::uint32_t type, int scale)
    {
        const double f = std::ldexp(1.0, -scale);
        switch (type) {
            case 4:
                return static_cast<double>(cpu.m_memory.read<std::uint8_t>(ea)) * f; // u8
            case 5:
                return static_cast<double>(cpu.m_memory.read<std::uint16_t>(ea)) * f; // u16
            case 6:
                return static_cast<double>(static_cast<std::int8_t>(cpu.m_memory.read<std::uint8_t>(ea))) * f; // s8
            case 7:
                return static_cast<double>(static_cast<std::int16_t>(cpu.m_memory.read<std::uint16_t>(ea))) * f; // s16
            default: { // 0: float
                const std::uint32_t raw = cpu.m_memory.read<std::uint32_t>(ea);
                float v;
                std::memcpy(&v, &raw, sizeof(v));
                return static_cast<double>(v);
            }
        }
    }

    static std::uint32_t elemSize(std::uint32_t type) { return (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4; }

    static void quant(Interpreter &cpu, std::uint32_t ea, std::uint32_t type, int scale, double value)
    {
        const double v = value * std::ldexp(1.0, scale);
        switch (type) {
            case 4:
                cpu.m_memory.write<std::uint8_t>(ea, static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0)));
                break;
            case 5:
                cpu.m_memory.write<std::uint16_t>(ea, static_cast<std::uint16_t>(std::clamp(v, 0.0, 65535.0)));
                break;
            case 6:
                cpu.m_memory.write<std::uint8_t>(ea, static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(v, -128.0, 127.0))));
                break;
            case 7:
                cpu.m_memory.write<std::uint16_t>(ea, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(v, -32768.0, 32767.0))));
                break;
            default: {
                const auto f = static_cast<float>(value);
                std::uint32_t raw;
                std::memcpy(&raw, &f, sizeof(raw));
                cpu.m_memory.write<std::uint32_t>(ea, raw);
            }
        }
    }

    /** @brief Paired Single Quantized Load. */
    void PSQ_L(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::int32_t disp = (i.psd & 0x800) ? static_cast<std::int32_t>(i.psd) - 0x1000 : static_cast<std::int32_t>(i.psd);
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + static_cast<std::uint32_t>(disp);
        const std::uint32_t gqr = cpu.m_gqr[i.psI & 0x7];
        const std::uint32_t type = (gqr >> 16) & 0x7;
        const int scale = scale6((gqr >> 24) & 0x3F);
        const std::uint32_t sz = elemSize(type);

        cpu.m_fpr[i.frt] = dequant(cpu, ea, type, scale);
        cpu.m_ps1[i.frt] = i.psW ? 1.0 : dequant(cpu, ea + sz, type, scale);
    }

    /** @brief Paired Single Quantized Store. */
    void PSQ_ST(Interpreter &cpu, const EncodedInstruction &i)
    {
        const std::int32_t disp = (i.psd & 0x800) ? static_cast<std::int32_t>(i.psd) - 0x1000 : static_cast<std::int32_t>(i.psd);
        const std::uint32_t ea = (i.ra == 0 ? 0 : cpu.m_gpr[i.ra]) + static_cast<std::uint32_t>(disp);
        const std::uint32_t gqr = cpu.m_gqr[i.psI & 0x7];
        const std::uint32_t type = gqr & 0x7;
        const int scale = scale6((gqr >> 8) & 0x3F);
        const std::uint32_t sz = elemSize(type);

        quant(cpu, ea, type, scale, cpu.m_fpr[i.frt]);
        if (!i.psW)
            quant(cpu, ea + sz, type, scale, cpu.m_ps1[i.frt]);
    }

} // namespace Core::Instruction
