/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_x64_jit_integer -- integer, compare, CR and SPR instructions against the interpreter
*/

#include "jit/JitHarness.hpp"

#ifdef WEMU_HAS_X64_JIT

namespace {
    constexpr int kRounds = 96;
}

TEST_F(X64JitTest, ArithmeticMatchesInterpreter)
{
    Random rng(10);
    // add addc adde addze addme subf subfc subfe subfze subfme neg mullw mulhw mulhwu divw divwu
    for (const unsigned xo: {266u, 10u, 138u, 202u, 234u, 40u, 8u, 136u, 200u, 232u, 104u, 235u, 75u, 11u, 491u, 459u})
        for (int round = 0; round < kRounds; ++round) {
            const std::array<std::uint32_t, 1> word{xForm(xo, reg(rng), reg(rng), reg(rng), rng() & 1, (rng() & 3) == 0)};
            expectSameBlock(word, [&] { randomize(rng); });
        }
}

TEST_F(X64JitTest, ImmediateArithmeticMatchesInterpreter)
{
    Random rng(11);
    // mulli subfic addic addic. addi addis ori oris xori xoris andi. andis.
    for (const unsigned op: {7u, 8u, 12u, 13u, 14u, 15u, 24u, 25u, 26u, 27u, 28u, 29u})
        for (int round = 0; round < kRounds; ++round) {
            const std::array<std::uint32_t, 1> word{dForm(op, reg(rng), (rng() & 7) == 0 ? 0 : reg(rng), value(rng))};
            expectSameBlock(word, [&] { randomize(rng); });
        }
}

TEST_F(X64JitTest, LogicAndShiftsMatchInterpreter)
{
    Random rng(12);
    // and andc or orc nor nand xor eqv extsb extsh cntlzw slw srw sraw srawi
    for (const unsigned xo: {28u, 60u, 444u, 412u, 124u, 476u, 316u, 284u, 954u, 922u, 26u, 24u, 536u, 792u, 824u})
        for (int round = 0; round < kRounds; ++round) {
            const std::array<std::uint32_t, 1> word{xForm(xo, reg(rng), reg(rng), reg(rng), rng() & 1)};
            expectSameBlock(word, [&] { randomize(rng); });
        }
}

TEST_F(X64JitTest, RotatesMatchInterpreter)
{
    Random rng(13);
    for (const unsigned op: {20u, 21u, 23u})
        for (int round = 0; round < kRounds * 2; ++round) {
            const std::array<std::uint32_t, 1> word{op << 26 | reg(rng) << 21 | reg(rng) << 16 | reg(rng) << 11 | reg(rng) << 6 | reg(rng) << 1 |
                                                    (rng() & 1)};
            expectSameBlock(word, [&] { randomize(rng); });
        }
}

TEST_F(X64JitTest, ComparesMatchInterpreter)
{
    Random rng(14);
    for (int round = 0; round < kRounds * 2; ++round) {
        const unsigned crf = rng() & 7;
        const std::array<std::uint32_t, 4> words{
                31u << 26 | crf << 23 | reg(rng) << 16 | reg(rng) << 11 | 0u << 1, // cmp
                31u << 26 | crf << 23 | reg(rng) << 16 | reg(rng) << 11 | 32u << 1, // cmpl
                11u << 26 | crf << 23 | reg(rng) << 16 | (value(rng) & 0xFFFF), // cmpi
                10u << 26 | crf << 23 | reg(rng) << 16 | (value(rng) & 0xFFFF), // cmpli
        };
        for (const auto word: words)
            expectSameBlock(std::array{word}, [&] { randomize(rng); });
    }
}

TEST_F(X64JitTest, ConditionRegisterLogicMatchesInterpreter)
{
    Random rng(15);
    // crand cror crxor crnand crnor crandc crorc creqv, then mcrf
    for (const unsigned xo: {257u, 449u, 193u, 225u, 33u, 129u, 417u, 289u})
        for (int round = 0; round < kRounds; ++round)
            expectSameBlock(std::array{19u << 26 | reg(rng) << 21 | reg(rng) << 16 | reg(rng) << 11 | xo << 1}, [&] { randomize(rng); });
    for (int round = 0; round < kRounds; ++round)
        expectSameBlock(std::array{19u << 26 | (rng() & 7) << 23 | (rng() & 7) << 18}, [&] { randomize(rng); });
}

TEST_F(X64JitTest, SpecialRegisterMovesMatchInterpreter)
{
    Random rng(16);
    const auto sprField = [](const unsigned spr) { return ((spr & 0x1F) << 5) | (spr >> 5); };
    for (const unsigned spr: {1u, 8u, 9u, 912u, 915u, 919u, 22u, 1008u})
        for (int round = 0; round < kRounds / 4; ++round) {
            expectSameBlock(std::array{31u << 26 | reg(rng) << 21 | sprField(spr) << 11 | 339u << 1}, [&] { randomize(rng); }); // mfspr
            expectSameBlock(std::array{31u << 26 | reg(rng) << 21 | sprField(spr) << 11 | 467u << 1}, [&] { randomize(rng); }); // mtspr
        }
    for (int round = 0; round < kRounds; ++round) {
        expectSameBlock(std::array{31u << 26 | reg(rng) << 21 | 19u << 1}, [&] { randomize(rng); }); // mfcr
        expectSameBlock(std::array{31u << 26 | reg(rng) << 21 | (rng() & 0xFF) << 12 | 144u << 1}, [&] { randomize(rng); }); // mtcrf
    }
}

TEST_F(X64JitTest, MixedIntegerBlocksMatchInterpreter)
{
    Random rng(17);
    const unsigned xos[] = {266u, 10u, 138u, 40u, 8u, 136u, 104u, 235u, 28u, 444u, 316u, 954u, 26u, 24u, 536u, 792u, 824u};
    for (int round = 0; round < kRounds * 4; ++round) {
        std::vector<std::uint32_t> words;
        for (int n = 0; n < 12; ++n)
            switch (rng() % 4) {
                case 0:
                    words.push_back(xForm(xos[rng() % std::size(xos)], reg(rng), reg(rng), reg(rng), rng() & 1));
                    break;
                case 1:
                    words.push_back(dForm(12 + rng() % 4, reg(rng), reg(rng), value(rng)));
                    break;
                case 2:
                    words.push_back(21u << 26 | reg(rng) << 21 | reg(rng) << 16 | reg(rng) << 11 | reg(rng) << 6 | reg(rng) << 1 | (rng() & 1));
                    break;
                default:
                    words.push_back(11u << 26 | (rng() & 7) << 23 | reg(rng) << 16 | (value(rng) & 0xFFFF));
                    break;
            }
        expectSameBlock(words, [&] { randomize(rng); });
    }
}

#endif
