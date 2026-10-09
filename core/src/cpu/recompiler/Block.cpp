#include "Block.hpp"

namespace Core::Ppc {
    namespace {
        std::optional<RegisterOperation> decodeOperation(std::uint32_t word)
        {
            const unsigned opcode = word >> 26, rt = (word >> 21) & 31;
            const unsigned ra = (word >> 16) & 31, rb = (word >> 11) & 31;
            const std::uint32_t immediate = word & 0xFFFF;
            const std::uint32_t signedImmediate = (immediate & 0x8000) ? immediate | 0xFFFF0000u : immediate;
            const Operand rs{false, rt}, a{false, ra}, b{false, rb};
            if (opcode == 14 || opcode == 15)
                return RegisterOperation{IntegerOp::Add, rt, ra ? a : Operand{true, 0},
                    {true, opcode == 15 ? immediate << 16 : signedImmediate}};
            if (opcode == 7)
                return RegisterOperation{IntegerOp::Multiply, rt, a, {true, signedImmediate}};
            if (opcode >= 24 && opcode <= 27)
                return RegisterOperation{opcode < 26 ? IntegerOp::Or : IntegerOp::Xor, ra, rs,
                    {true, (opcode & 1) ? immediate << 16 : immediate}};
            if (word & 1) return std::nullopt; // Record forms update CR0 outside this ABI.
            if (opcode == 20 || opcode == 21 || opcode == 23) {
                const unsigned mb = (word >> 6) & 31, me = (word >> 1) & 31;
                std::uint32_t mask = 0;
                for (unsigned i = 0; i < 32; ++i)
                    if (mb <= me ? (i >= mb && i <= me) : (i >= mb || i <= me))
                        mask |= 0x80000000u >> i;
                return RegisterOperation{opcode == 20 ? IntegerOp::RotateInsert : IntegerOp::RotateMask,
                    ra, rs, opcode == 23 ? b : Operand{true, rb}, mask};
            }
            if (opcode == 31) {
                // Full ten-bit XO also rejects OE=1 arithmetic variants.
                switch ((word >> 1) & 1023) {
                    case 266: return RegisterOperation{IntegerOp::Add, rt, a, b};
                    case 40: return RegisterOperation{IntegerOp::Subtract, rt, b, a};
                    case 235: return RegisterOperation{IntegerOp::Multiply, rt, a, b};
                    case 28: return RegisterOperation{IntegerOp::And, ra, rs, b};
                    case 444: return RegisterOperation{IntegerOp::Or, ra, rs, b};
                    case 316: return RegisterOperation{IntegerOp::Xor, ra, rs, b};
                    case 24: return RegisterOperation{IntegerOp::ShiftLeft, ra, rs, b};
                    case 536: return RegisterOperation{IntegerOp::ShiftRight, ra, rs, b};
                    default: break;
                }
            }
            return std::nullopt;
        }
    }

    bool Block::supports(std::uint32_t word)
    {
        return decodeOperation(word).has_value();
    }

    std::optional<Block> Block::decode(std::uint32_t pc, std::span<const std::uint32_t> words)
    {
        if ((pc & 3) || words.empty() || words.size() > 64
            || std::uint64_t(pc) + words.size() * 4 > UINT32_MAX)
            return std::nullopt;
        Block block;
        block.m_pc = pc;
        block.m_words.assign(words.begin(), words.end());
        for (const auto word : words) {
            const auto operation = decodeOperation(word);
            if (!operation) return std::nullopt;
            block.m_operations.push_back(*operation);
        }
        return block;
    }
}
