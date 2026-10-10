#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Shared Latte decoding for reference execution and future native shader lowering.
// ALU groups are decoded; CF and texture words remain available in their original form.
namespace Core::Gfx::Latte {

    // ---- encodings (AMD R600 ISA, cross-checked against decaf-emu latte_instructions) ----

    // CF_WORD1 [second dword of a generic CF instruction]
    constexpr std::uint32_t cfInst(std::uint32_t w1) { return (w1 >> 23) & 0x7F; }
    constexpr bool cfEop(std::uint32_t w1) { return (w1 >> 21) & 1; }

    enum CfOp : std::uint32_t {
        CF_NOP = 0x00,
        CF_TEX = 0x01,
        CF_VTX = 0x02,
        CF_VTX_TC = 0x03,
        CF_CALL_FS = 0x13,
        CF_RETURN = 0x14,
        CF_EXP = 0x27,
        CF_EXP_DONE = 0x28,
    };
    // CF_ALU_WORD1: CF_INST is 4 bits at 26-29. Values 8..15 are all ALU clause variants
    // (ALU, ALU_PUSH_BEFORE, ALU_POP_AFTER, ALU_POP2_AFTER, ALU_CONTINUE, ALU_BREAK,
    // ALU_ELSE_AFTER). Execution backends implement their predicate-stack effects.
    constexpr std::uint32_t cfAluInst(std::uint32_t w1) { return (w1 >> 26) & 0xF; }
    constexpr bool isAluClause(std::uint32_t w1) { return cfAluInst(w1) >= 0x8; }

    // ALU_WORD0
    struct Src {
            std::uint32_t sel, chan;
            bool rel, neg, abs;
    };
    // ALU special source selectors. LLVM's R600 register table names 250 as ONE_INT; it must
    // be represented as integer bits when supplied to *_INT operations.
    enum : std::uint32_t { SRC_0 = 248, SRC_1 = 249, SRC_1_INT = 250, SRC_HALF = 252, SRC_LITERAL = 253, SRC_PV = 254, SRC_PS = 255 };

    enum Op2 : std::uint32_t {
        OP2_ADD = 0x00,
        OP2_MUL = 0x01,
        OP2_MUL_IEEE = 0x02,
        OP2_MAX = 0x03,
        OP2_MIN = 0x04,
        OP2_MAX_DX10 = 0x05,
        OP2_MIN_DX10 = 0x06,
        OP2_SETE = 0x08,
        OP2_SETGT = 0x09,
        OP2_SETGE = 0x0A,
        OP2_SETNE = 0x0B,
        OP2_SETE_DX10 = 0x0C,
        OP2_SETGT_DX10 = 0x0D,
        OP2_SETGE_DX10 = 0x0E,
        OP2_SETNE_DX10 = 0x0F,
        OP2_FRACT = 0x10,
        OP2_RNDNE = 0x13,
        OP2_FLOOR = 0x14,
        OP2_MOV = 0x19,
        OP2_NOP = 0x1A,
        OP2_PRED_SETE = 0x20,
        OP2_PRED_SETGT = 0x21,
        OP2_PRED_SETGE = 0x22,
        OP2_PRED_SETNE = 0x23,
        OP2_PRED_SETE_PUSH = 0x28,
        OP2_PRED_SETGT_PUSH = 0x29,
        OP2_PRED_SETGE_PUSH = 0x2A,
        OP2_PRED_SETNE_PUSH = 0x2B,
        OP2_AND_INT = 0x30,
        OP2_OR_INT = 0x31,
        OP2_XOR_INT = 0x32,
        OP2_ADD_INT = 0x34,
        OP2_SUB_INT = 0x35,
        OP2_SETE_INT = 0x3A,
        OP2_SETGT_INT = 0x3B,
        OP2_SETGE_INT = 0x3C,
        OP2_SETNE_INT = 0x3D,
        OP2_PRED_SETE_INT = 0x42,
        OP2_PRED_SETGT_INT = 0x43,
        OP2_PRED_SETGE_INT = 0x44,
        OP2_PRED_SETNE_INT = 0x45,
        OP2_DOT4 = 0x50,
        OP2_DOT4_IEEE = 0x51,
        OP2_EXP_IEEE = 0x61,
        OP2_LOG_CLAMPED = 0x62,
        OP2_LOG_IEEE = 0x63,
        OP2_RECIP_IEEE = 0x66,
        OP2_RECIPSQRT_IEEE = 0x69,
        OP2_FLT_TO_INT = 0x6B,
        OP2_INT_TO_FLT = 0x6C,
        OP2_UINT_TO_FLT = 0x6D,
        OP2_ASHR = 0x70,
        OP2_LSHR = 0x71,
        OP2_LSHL = 0x72,
        OP2_FLT_TO_UINT = 0x79,
    };

    enum Op3 : std::uint32_t {
        OP3_MULADD = 0x10,
        OP3_MULADD_M2 = 0x11,
        OP3_MULADD_M4 = 0x12,
        OP3_MULADD_D2 = 0x13,
        OP3_MULADD_IEEE = 0x14,
        OP3_MULADD_IEEE_M2 = 0x15,
        OP3_MULADD_IEEE_M4 = 0x16,
        OP3_MULADD_IEEE_D2 = 0x17,
        OP3_CNDE = 0x18,
        OP3_CNDGT = 0x19,
        OP3_CNDGE = 0x1A,
        OP3_CNDE_INT = 0x1C,
        OP3_CNDGT_INT = 0x1D,
        OP3_CNDGE_INT = 0x1E,
    };

    struct AluInst {
            std::uint32_t raw0{}, raw1{};
            Src src[3]{};
            std::uint32_t op{0};
            bool op3{false};
            std::uint32_t dstGpr{0}, dstChan{0};
            bool writeMask{true}, clamp{false}, last{false};
            std::uint32_t omod{0};
            std::uint32_t predSel{0}; // 0 = always, 2 = execute when pred==0, 3 = when pred==1
            bool scalarSlot{false};
            float literal[4]{}; // group literal dwords (filled after group decode)

            [[nodiscard]] bool isPredSet() const
            {
                return !op3 && ((op >= OP2_PRED_SETE && op <= OP2_PRED_SETNE_PUSH) || (op >= OP2_PRED_SETE_INT && op <= OP2_PRED_SETNE_INT));
            }
    };

    struct Program {
            std::vector<std::uint32_t> words;
            std::unordered_map<std::uint64_t, std::vector<std::vector<AluInst>>> clauses;
            bool valid{true};
            unsigned registerCount{5};
    };

    // Structural validity is not a guarantee of backend opcode or control-flow support.
    // No cache or execution state: callers own caching and capability validation.
    std::shared_ptr<const Program> decodeProgram(const std::vector<std::uint32_t> &words);

} // namespace Core::Gfx::Latte
