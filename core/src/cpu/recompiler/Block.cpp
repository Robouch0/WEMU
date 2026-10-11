#include "Block.hpp"

namespace Core::Ppc {
    namespace {
        std::optional<RegisterOperation> decodeBase(std::uint32_t word)
        {
            const unsigned opcode = word >> 26, rt = (word >> 21) & 31;
            const unsigned ra = (word >> 16) & 31, rb = (word >> 11) & 31;
            const std::uint32_t immediate = word & 0xFFFF;
            const std::uint32_t signedImmediate = (immediate & 0x8000) ? immediate | 0xFFFF0000u : immediate;
            const Operand rs{false, rt}, a{false, ra}, b{false, rb};
            if (opcode == 10 || opcode == 11) {
                if (word & (3u << 21))
                    return std::nullopt; // Reject 64-bit and reserved comparison encodings.
                RegisterOperation op{opcode == 11 ? IntegerOp::CompareSigned : IntegerOp::CompareUnsigned,
                                     0,
                                     a,
                                     {true, opcode == 11 ? signedImmediate : immediate}};
                op.conditionField = (word >> 23) & 7;
                return op;
            }
            if (opcode == 18)
                return RegisterOperation{IntegerOp::Branch, 0, {true, 0}, {true, 0}, word};
            if (opcode == 16 || opcode == 19 && ((word >> 1) & 1023) == 16) {
                // Keep conditional link forms on the reference path while their
                // existing taken-only LR policy remains outside this backend.
                if (word & 1)
                    return std::nullopt;
                return RegisterOperation{opcode == 16 ? IntegerOp::BranchConditional : IntegerOp::BranchLinkRegister, 0, {true, 0}, {true, 0}, word};
            }
            if (opcode == 28 || opcode == 29) {
                RegisterOperation op{IntegerOp::And, ra, rs, {true, opcode == 29 ? immediate << 16 : immediate}};
                op.record = true;
                return op;
            }
            if (opcode == 14 || opcode == 15)
                return RegisterOperation{IntegerOp::Add, rt, ra ? a : Operand{true, 0}, {true, opcode == 15 ? immediate << 16 : signedImmediate}};
            if (opcode == 7)
                return RegisterOperation{IntegerOp::Multiply, rt, a, {true, signedImmediate}};
            if (opcode >= 24 && opcode <= 27)
                return RegisterOperation{opcode < 26 ? IntegerOp::Or : IntegerOp::Xor, ra, rs, {true, (opcode & 1) ? immediate << 16 : immediate}};
            if (opcode >= 32 && opcode <= 45) {
                const bool load = opcode <= 35 || opcode >= 40 && opcode <= 43;
                const bool update = opcode & 1;
                if (update && (!ra || load && ra == rt))
                    return std::nullopt;
                const unsigned bytes = opcode <= 33 || opcode == 36 || opcode == 37 ? 4 : opcode <= 39 ? 1 : 2;
                return RegisterOperation{load ? IntegerOp::Load : IntegerOp::Store,
                                         rt,
                                         ra ? a : Operand{true, 0},
                                         {true, signedImmediate},
                                         0xFFFFFFFFu,
                                         bytes,
                                         opcode == 42 || opcode == 43,
                                         update ? ra : 0};
            }
            if (opcode == 20 || opcode == 21 || opcode == 23) {
                const unsigned mb = (word >> 6) & 31, me = (word >> 1) & 31;
                std::uint32_t mask = 0;
                for (unsigned i = 0; i < 32; ++i)
                    if (mb <= me ? (i >= mb && i <= me) : (i >= mb || i <= me))
                        mask |= 0x80000000u >> i;
                return RegisterOperation{opcode == 20 ? IntegerOp::RotateInsert : IntegerOp::RotateMask, ra, rs, opcode == 23 ? b : Operand{true, rb},
                                         mask};
            }
            if (opcode == 31) {
                // Full ten-bit XO also rejects OE=1 arithmetic variants.
                const unsigned xo = (word >> 1) & 1023;
                const bool load = xo == 23 || xo == 55 || xo == 87 || xo == 119 || xo == 279 || xo == 311 || xo == 343 || xo == 375;
                const bool store = xo == 151 || xo == 183 || xo == 215 || xo == 247 || xo == 407 || xo == 439;
                if (load || store) {
                    if (word & 1)
                        return std::nullopt;
                    const bool update = xo & 32;
                    if (update && (!ra || load && ra == rt))
                        return std::nullopt;
                    const unsigned bytes = xo == 23 || xo == 55 || xo == 151 || xo == 183    ? 4
                                           : xo == 87 || xo == 119 || xo == 215 || xo == 247 ? 1
                                                                                             : 2;
                    return RegisterOperation{load ? IntegerOp::Load : IntegerOp::Store,
                                             rt,
                                             ra ? a : Operand{true, 0},
                                             b,
                                             0xFFFFFFFFu,
                                             bytes,
                                             xo == 343 || xo == 375,
                                             update ? ra : 0};
                }
                if (xo == 0 || xo == 32) {
                    if (word & ((3u << 21) | 1u))
                        return std::nullopt;
                    RegisterOperation op{xo == 0 ? IntegerOp::CompareSigned : IntegerOp::CompareUnsigned, 0, a, b};
                    op.conditionField = (word >> 23) & 7;
                    return op;
                }
                if (xo == 339 || xo == 467) {
                    if (word & 1)
                        return std::nullopt;
                    const unsigned spr = ra | rb << 5;
                    if (spr != 8 && spr != 9)
                        return std::nullopt;
                    return RegisterOperation{xo == 339 ? (spr == 8 ? IntegerOp::ReadLink : IntegerOp::ReadCounter)
                                                       : (spr == 8 ? IntegerOp::WriteLink : IntegerOp::WriteCounter),
                                             rt,
                                             rs,
                                             {true, 0}};
                }
                switch ((word >> 1) & 1023) {
                    case 104:
                        return RegisterOperation{IntegerOp::Negate, rt, a, {true, 0}};
                    case 922:
                        return RegisterOperation{IntegerOp::SignExtend16, ra, rs, {true, 0}};
                    case 954:
                        return RegisterOperation{IntegerOp::SignExtend8, ra, rs, {true, 0}};
                    case 26:
                        return RegisterOperation{IntegerOp::LeadingZeros, ra, rs, {true, 0}};
                    case 266:
                        return RegisterOperation{IntegerOp::Add, rt, a, b};
                    case 40:
                        return RegisterOperation{IntegerOp::Subtract, rt, b, a};
                    case 235:
                        return RegisterOperation{IntegerOp::Multiply, rt, a, b};
                    case 28:
                        return RegisterOperation{IntegerOp::And, ra, rs, b};
                    case 444:
                        return RegisterOperation{IntegerOp::Or, ra, rs, b};
                    case 316:
                        return RegisterOperation{IntegerOp::Xor, ra, rs, b};
                    case 24:
                        return RegisterOperation{IntegerOp::ShiftLeft, ra, rs, b};
                    case 536:
                        return RegisterOperation{IntegerOp::ShiftRight, ra, rs, b};
                    default:
                        break;
                }
            }
            return std::nullopt;
        }
        std::optional<RegisterOperation> decodeOperation(std::uint32_t word)
        {
            auto op = decodeBase(word);
            if (op && (word & 1) && ((word >> 26) == 20 || (word >> 26) == 21 || (word >> 26) == 23 || (word >> 26) == 31))
                op->record = true;
            return op;
        }
    } // namespace

    bool Block::supports(std::uint32_t word) { return decodeOperation(word).has_value(); }

    bool Block::stores(std::uint32_t word)
    {
        const auto op = decodeOperation(word);
        return op && op->op == IntegerOp::Store;
    }
    bool Block::branches(std::uint32_t word)
    {
        const auto op = decodeOperation(word);
        return op && (op->op == IntegerOp::Branch || op->op == IntegerOp::BranchConditional || op->op == IntegerOp::BranchLinkRegister);
    }

    bool Block::loopsToStart(std::uint32_t pc, std::span<const std::uint32_t> words)
    {
        if ((pc & 3) || words.empty() || words.size() > 32 || std::uint64_t(pc) + words.size() * 4 > UINT32_MAX)
            return false;
        for (unsigned i = 0; i < words.size(); ++i) {
            const auto operation = decodeOperation(words[i]);
            if (!operation)
                return false;
            if ((operation->op == IntegerOp::Branch || operation->op == IntegerOp::BranchConditional ||
                 operation->op == IntegerOp::BranchLinkRegister) &&
                i + 1 < words.size())
                return false;
        }
        const auto last = decodeOperation(words.back());
        if (!last)
            return false;
        const bool direct = last->op == IntegerOp::Branch;
        if (!direct && last->op != IntegerOp::BranchConditional)
            return false;
        const auto word = words.back();
        std::uint32_t offset = direct ? word & 0x03FFFFFCu : word & 0xFFFCu;
        if (offset & (direct ? 0x02000000u : 0x8000u))
            offset |= direct ? 0xFC000000u : 0xFFFF0000u;
        const std::uint32_t branchPc = pc + static_cast<std::uint32_t>((words.size() - 1) * 4);
        return ((word & 2) ? offset : branchPc + offset) == pc;
    }

    std::optional<Block> Block::decode(std::uint32_t pc, std::span<const std::uint32_t> words)
    {
        if ((pc & 3) || words.empty() || words.size() > 64 || std::uint64_t(pc) + words.size() * 4 > UINT32_MAX)
            return std::nullopt;
        Block block;
        block.m_pc = pc;
        block.m_words.assign(words.begin(), words.end());
        for (const auto word: words) {
            const auto operation = decodeOperation(word);
            if (!operation)
                return std::nullopt;
            block.m_operations.push_back(*operation);
            if ((operation->op == IntegerOp::Branch || operation->op == IntegerOp::BranchConditional ||
                 operation->op == IntegerOp::BranchLinkRegister) &&
                block.m_operations.size() != words.size())
                return std::nullopt; // No instruction may follow a control-flow terminator.
        }
        return block;
    }
} // namespace Core::Ppc
