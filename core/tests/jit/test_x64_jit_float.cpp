/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_x64_jit_float -- floating-point and paired-single instructions against the interpreter
*/

#include "jit/JitHarness.hpp"

#ifdef WEMU_HAS_X64_JIT

namespace {
    constexpr int kRounds = 128;

    std::uint32_t aForm(const unsigned op, const unsigned xo, const std::uint32_t t, const std::uint32_t a, const std::uint32_t b,
                        const std::uint32_t c, const bool rc)
    {
        return op << 26 | t << 21 | a << 16 | b << 11 | c << 6 | xo << 1 | (rc ? 1 : 0);
    }
} // namespace

class X64JitFloatTest : public X64JitTest {
    protected:
        void setup(Random &rng)
        {
            randomize(rng);
            randomizeFloat(rng);
        }
};

TEST_F(X64JitFloatTest, DoubleArithmeticMatchesInterpreter)
{
    Random rng(40);
    // fadd fsub fmul fdiv fsqrt fsel frsqrte fmadd fmsub fnmadd fnmsub
    for (const unsigned xo: {21u, 20u, 25u, 18u, 22u, 23u, 26u, 29u, 28u, 31u, 30u})
        for (int round = 0; round < kRounds; ++round)
            expectSameBlock(std::array{aForm(63, xo, reg(rng), reg(rng), reg(rng), reg(rng), rng() & 1)}, [&] { setup(rng); });
}

TEST_F(X64JitFloatTest, SingleArithmeticMatchesInterpreter)
{
    Random rng(41);
    // fadds fsubs fmuls fdivs fres fmadds fmsubs fnmadds fnmsubs
    for (const unsigned xo: {21u, 20u, 25u, 18u, 24u, 29u, 28u, 31u, 30u})
        for (int round = 0; round < kRounds; ++round)
            expectSameBlock(std::array{aForm(59, xo, reg(rng), reg(rng), reg(rng), reg(rng), rng() & 1)}, [&] { setup(rng); });
}

TEST_F(X64JitFloatTest, MovesConversionsAndComparesMatchInterpreter)
{
    Random rng(42);
    // frsp fctiw fctiwz fnabs fneg fmr fabs, then fcmpu
    for (const unsigned xo: {12u, 14u, 15u, 136u, 40u, 72u, 264u})
        for (int round = 0; round < kRounds; ++round)
            expectSameBlock(std::array{63u << 26 | reg(rng) << 21 | reg(rng) << 11 | xo << 1 | (rng() & 1)}, [&] { setup(rng); });
    for (int round = 0; round < kRounds * 2; ++round)
        expectSameBlock(std::array{63u << 26 | (rng() & 7) << 23 | reg(rng) << 16 | reg(rng) << 11}, [&] { setup(rng); });
}

TEST_F(X64JitFloatTest, PairedArithmeticMatchesInterpreter)
{
    Random rng(43);
    // ps_add sub mul div sum0 sum1 muls0 muls1 madds0 madds1 sel res rsqrte madd msub nmadd nmsub
    for (const unsigned xo: {21u, 20u, 25u, 18u, 10u, 11u, 12u, 13u, 14u, 15u, 23u, 24u, 26u, 29u, 28u, 31u, 30u})
        for (int round = 0; round < kRounds; ++round)
            expectSameBlock(std::array{aForm(4, xo, reg(rng), reg(rng), reg(rng), reg(rng), rng() & 1)}, [&] { setup(rng); });
}

TEST_F(X64JitFloatTest, PairedMovesAndComparesMatchInterpreter)
{
    Random rng(44);
    // ps_mr ps_neg ps_abs ps_nabs ps_merge00 01 10 11
    for (const unsigned xo: {72u, 40u, 264u, 136u, 528u, 560u, 592u, 624u})
        for (int round = 0; round < kRounds; ++round)
            expectSameBlock(std::array{4u << 26 | reg(rng) << 21 | reg(rng) << 16 | reg(rng) << 11 | xo << 1 | (rng() & 1)}, [&] { setup(rng); });
    for (int round = 0; round < kRounds * 2; ++round) // ps_cmpo0
        expectSameBlock(std::array{4u << 26 | (rng() & 7) << 23 | reg(rng) << 16 | reg(rng) << 11 | 32u << 1}, [&] { setup(rng); });
}

TEST_F(X64JitFloatTest, FloatLoadsAndStoresMatchInterpreter)
{
    Random rng(45);
    struct Form {
            unsigned code;
            bool indexed;
    };
    // lfs lfsu lfd lfdu stfs stfsu stfd stfdu, lfsx lfsux lfdx stfsx stfsux stfdx stfiwx
    const Form forms[] = {{48, false}, {49, false}, {50, false}, {51, false}, {52, false}, {53, false}, {54, false}, {55, false},
                          {535, true}, {567, true}, {599, true}, {663, true}, {695, true}, {727, true}, {983, true}};
    for (int round = 0; round < kRounds / 2; ++round) {
        for (unsigned i = 0; i < kDataBytes; i += 8)
            cpu->m_memory.write<std::uint64_t>(kData + i, floatBits(rng));
        for (const auto &form: forms)
            for (unsigned kind = 0; kind < 4; ++kind) {
                const std::uint32_t ra = 1 + rng() % 31, rb = reg(rng);
                const std::uint32_t ea = kind == 0   ? kData + (rng() % (kDataBytes - 8))
                                         : kind == 1 ? kStack + (rng() % (kWindowBytes - 8))
                                         : kind == 2 ? kCodeData + (rng() & 0xF8)
                                                     : 0x2000 + (rng() & 0xFF);
                const std::uint32_t word = form.indexed ? xForm(form.code, reg(rng), ra, rb) : dForm(form.code, reg(rng), ra, rng());
                expectSameBlock(std::array{word}, [&] {
                    setup(rng);
                    if (form.indexed) {
                        cpu->m_gpr[ra] = ra == rb ? ea / 2 : rng();
                        if (ra != rb)
                            cpu->m_gpr[rb] = ea - cpu->m_gpr[ra];
                    } else {
                        cpu->m_gpr[ra] = ea - static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xFFFF)));
                    }
                });
            }
    }
}

TEST_F(X64JitFloatTest, QuantizedLoadsAndStoresMatchInterpreter)
{
    Random rng(46);
    for (int round = 0; round < kRounds * 2; ++round) {
        for (unsigned i = 0; i < kDataBytes; i += 4)
            cpu->m_memory.write<std::uint32_t>(kData + i, (rng() & 1) ? rng() : static_cast<std::uint32_t>(floatBits(rng) >> 32));
        std::uint32_t ra = rng() & 7 ? 1 + rng() % 31 : 0;
        const std::uint32_t gqrIndex = rng() & 7, disp = rng() & 0xFFF;
        const std::uint32_t op = std::array{56u, 57u, 60u, 61u}[rng() & 3];
        if ((op & 1) && !ra)
            ra = 8; // update forms require a nonzero base register
        const std::uint32_t word = op << 26 | reg(rng) << 21 | ra << 16 | (rng() & 1) << 15 | gqrIndex << 12 | disp;
        const auto signedDisp =
                static_cast<std::uint32_t>((disp & 0x800) ? static_cast<std::int32_t>(disp) - 0x1000 : static_cast<std::int32_t>(disp));
        const unsigned kind = rng() % 4;
        const std::uint32_t ea = kind == 0   ? kData + (rng() % (kDataBytes - 8))
                                 : kind == 1 ? kStack + (rng() % (kWindowBytes - 8))
                                 : kind == 2 ? kData + kDataBytes - 4 // second lane past the scratch: still mapped
                                             : 0x2000 + (rng() & 0xFF);
        expectSameBlock(std::array{word}, [&] {
            setup(rng);
            if (ra)
                cpu->m_gpr[ra] = ea - signedDisp;
            // GQR: load type/scale in bits 16-18/24-29, store type/scale in bits 0-2/8-13.
            const std::uint32_t type = (rng() & 1) ? rng() & 7 : 0;
            const std::uint32_t scale = rng() & 0x3F;
            cpu->m_gqr[gqrIndex] = type << 16 | scale << 24 | type | scale << 8;
        });
    }
}

TEST_F(X64JitFloatTest, MixedFloatBlocksMatchInterpreter)
{
    Random rng(47);
    for (int round = 0; round < kRounds * 2; ++round) {
        std::vector<std::uint32_t> words;
        for (int n = 0; n < 12; ++n)
            switch (rng() % 5) {
                case 0:
                    words.push_back(aForm(59, std::array{18u, 20u, 21u, 24u, 25u}[rng() % 5], reg(rng), reg(rng), reg(rng), reg(rng), false));
                    break;
                case 1:
                    words.push_back(aForm(4, 10 + rng() % 6, reg(rng), reg(rng), reg(rng), reg(rng), false));
                    break;
                case 2:
                    words.push_back(dForm(48 + 2 * (rng() % 4), reg(rng), 30, rng() & 0x3F8)); // lfs lfd stfs stfd
                    break;
                case 3:
                    words.push_back(56u << 26 | reg(rng) << 21 | 31u << 16 | (rng() & 0x3F8));
                    break;
                default:
                    words.push_back(aForm(63, 29 + rng() % 3, reg(rng), reg(rng), reg(rng), reg(rng), false));
                    break;
            }
        expectSameBlock(words, [&] {
            setup(rng);
            cpu->m_gpr[30] = kData + 0x400;
            cpu->m_gpr[31] = kStack + 0x400;
            cpu->m_gqr[0] = 0;
        });
    }
}

TEST_F(X64JitFloatTest, QuantizedUpdateCopyLoopWithCacheInvalidationMatchesInterpreter)
{
    std::vector<std::uint32_t> words{(31u << 26) | (8u << 11) | (470u << 1)}; // dcbi 0,r8
    for (unsigned block = 0; block < 4; ++block)
        for (const unsigned raw: {0xE0080008u, 0xF00A0008u, 0xE0080010u, 0xF00A0010u, 0xE0080018u, 0xF00A0018u, 0xE4080020u, 0xF40A0020u})
            words.push_back(raw);
    expectSameBlock(words, [&] {
        for (unsigned i = 0; i < 32; ++i)
            cpu->m_memory.write<std::uint32_t>(kData + i * 4, std::bit_cast<std::uint32_t>(float(i) + .25f));
        cpu->m_gpr[8] = kData - 8;
        cpu->m_gpr[10] = kData + 0x400 - 8;
        cpu->m_gqr[0] = 0;
    });
}

#endif
