#include "gfx/LatteProgram.hpp"

#include <algorithm>
#include <bit>

namespace Core::Gfx::Latte {

    std::shared_ptr<const Program> decodeProgram(const std::vector<std::uint32_t> &words)
    {
        auto result = std::make_shared<Program>();
        result->words = words;
        for (std::size_t cf = 0; cf + 1 < words.size(); cf += 2) {
            const auto cf0 = words[cf], cf1 = words[cf + 1];
            if (!isAluClause(cf1)) {
                const auto op = cfInst(cf1);
                if (op == CF_EXP || op == CF_EXP_DONE)
                    result->registerCount = std::max(result->registerCount, ((cf0 >> 15) & 127) + 1);
                if (op == CF_TEX) {
                    const auto count = (((cf1 >> 10) & 7) | ((cf1 >> 16) & 8)) + 1;
                    for (unsigned i = 0; i < count; i++) {
                        const auto off = std::uint64_t(cf0) * 2 + i * 4;
                        if (off + 3 >= words.size()) {
                            result->valid = false;
                            break;
                        }
                        result->registerCount = std::max({result->registerCount, ((words[off] >> 16) & 127) + 1, (words[off + 1] & 127) + 1});
                    }
                }
                if (cfEop(cf1) || op == CF_RETURN)
                    break;
                continue;
            }
            const auto addr = cf0 & 0x3FFFFF, count = ((cf1 >> 18) & 0x7F) + 1;
            auto &groups = result->clauses[(std::uint64_t(addr) << 32) | count];
            if (!groups.empty())
                continue;
            auto slot = addr;
            const auto end = addr + count;
            if (std::uint64_t(end) * 2 > words.size()) {
                result->valid = false;
                break;
            }
            while (slot < end) {
                std::vector<AluInst> group;
                bool usesLiteral = false;
                std::uint32_t maxLitChan = 0, vectorSlots = 0;
                do {
                    const auto w0 = words[slot * 2], w1 = words[slot * 2 + 1];
                    ++slot;
                    AluInst in;
                    in.raw0 = w0;
                    in.raw1 = w1;
                    in.src[0] = {w0 & 0x1FF, (w0 >> 10) & 3, bool(w0 & (1u << 9)), bool(w0 & (1u << 12)), false};
                    in.src[1] = {(w0 >> 13) & 0x1FF, (w0 >> 23) & 3, bool(w0 & (1u << 22)), bool(w0 & (1u << 25)), false};
                    in.last = (w0 >> 31) & 1;
                    in.predSel = (w0 >> 29) & 3;
                    in.dstGpr = (w1 >> 21) & 127;
                    result->registerCount = std::max(result->registerCount, in.dstGpr + 1);
                    in.dstChan = (w1 >> 29) & 3;
                    in.clamp = (w1 >> 31) & 1;
                    const auto op3Field = (w1 >> 13) & 31;
                    in.op3 = op3Field >= 8;
                    if (in.op3) {
                        in.op = op3Field;
                        in.src[2] = {w1 & 0x1FF, (w1 >> 10) & 3, bool(w1 & (1u << 9)), bool(w1 & (1u << 12)), false};
                    } else {
                        in.op = (w1 >> 7) & 0x7FF;
                        in.src[0].abs = w1 & 1;
                        in.src[1].abs = w1 & 2;
                        in.writeMask = w1 & 16;
                        in.omod = (w1 >> 5) & 3;
                    }
                    for (const auto &src: in.src) {
                        if (src.sel < 128)
                            result->registerCount = std::max(result->registerCount, src.sel + 1);
                        if (src.sel == SRC_LITERAL) {
                            usesLiteral = true;
                            maxLitChan = std::max(maxLitChan, src.chan);
                        }
                    }
                    const bool transOnly = !in.op3 && ((in.op >= 0x60 && in.op <= 0x6D) || in.op == OP2_FLT_TO_UINT);
                    in.scalarSlot = transOnly || (vectorSlots & (1u << in.dstChan));
                    if (!in.scalarSlot)
                        vectorSlots |= 1u << in.dstChan;
                    group.push_back(in);
                } while (!group.back().last && slot < end && group.size() < 5);
                if (!group.back().last) {
                    result->valid = false;
                    break;
                }
                if (usesLiteral) {
                    const auto literalSlots = maxLitChan / 2 + 1;
                    if (slot + literalSlots > end) {
                        result->valid = false;
                        break;
                    }
                    for (auto &in: group)
                        for (unsigned i = 0; i < literalSlots * 2; i++)
                            in.literal[i] = std::bit_cast<float>(words[slot * 2 + i]);
                    slot += literalSlots;
                }
                groups.push_back(std::move(group));
            }
            if (!result->valid)
                break;
        }
        return result;
    }

} // namespace Core::Gfx::Latte
