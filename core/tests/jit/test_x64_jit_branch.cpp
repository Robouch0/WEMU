/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_x64_jit_branch -- branches against the interpreter
*/

#include "jit/JitHarness.hpp"

#ifdef WEMU_HAS_X64_JIT

TEST_F(X64JitTest, DirectBranchesMatchInterpreter)
{
    Random rng(30);
    for (int round = 0; round < 256; ++round) {
        const std::uint32_t word = 18u << 26 | (rng() & 0x03FFFFFC) | (rng() & 3); // b, ba, bl, bla
        expectSameBlock(std::array{0x38600001u, word}, [&] { randomize(rng); });
    }
}

TEST_F(X64JitTest, ConditionalBranchesMatchInterpreter)
{
    Random rng(31);
    for (int round = 0; round < 1024; ++round) {
        const std::uint32_t bo = rng() & 31, bi = rng() & 31, flags = rng() & 3;
        const std::uint32_t bc = 16u << 26 | bo << 21 | bi << 16 | (rng() & 0xFFFC) | flags;
        const std::uint32_t bclr = 19u << 26 | bo << 21 | bi << 16 | 16u << 1 | (flags & 1);
        const std::uint32_t bcctr = 19u << 26 | bo << 21 | bi << 16 | 528u << 1 | (flags & 1);
        for (const auto word: {bc, bclr, bcctr})
            expectSameBlock(std::array{word}, [&] {
                randomize(rng);
                if (rng() & 1)
                    cpu->m_ctr = rng() & 3; // ends of the decrement-and-test range
                else if ((rng() & 7) == 0)
                    cpu->m_ctr = 0xC0000000 + (rng() & 0xFC); // import sentinel: HLE through the interpreter
            });
    }
}

TEST_F(X64JitTest, CompareThenBranchMatchesInterpreter)
{
    // A compare or record form right before bc: the branch reads the CR value still in a register.
    Random rng(32);
    for (int round = 0; round < 512; ++round) {
        const std::uint32_t crf = rng() & 7, bo = rng() & 31, bi = (rng() & 1) ? crf * 4 + (rng() & 3) : rng() & 31;
        const std::uint32_t compareOp = (rng() & 1) ? 11u : 10u; // cmpi / cmpli
        const std::uint32_t first = (rng() % 3) == 0 ? xForm(266, reg(rng), reg(rng), reg(rng), true) // add.
                                                     : compareOp << 26 | crf << 23 | reg(rng) << 16 | (value(rng) & 0xFFFF);
        const std::uint32_t second = (rng() % 3) == 0 ? 19u << 26 | bo << 21 | bi << 16 | 16u << 1 // bclr
                                                      : 16u << 26 | bo << 21 | bi << 16 | (rng() & 0xFFFC);
        expectSameBlock(std::array{first, second}, [&] { randomize(rng); });
    }
}

TEST_F(X64JitTest, CountedLoopsMatchInterpreter)
{
    // li r3,0 ; mtctr r4 ; loop: addi r3,r3,3 ; bdnz loop
    const std::array<std::uint32_t, 2> loop{0x38630003, 0x4200FFFC};
    load(loop);
    for (const std::uint32_t count: {1u, 2u, 7u, 100u}) {
        cpu->m_pc = kCode;
        cpu->m_gpr[3] = 0;
        cpu->m_ctr = count;
        std::uint64_t retired = 0, lastStore = 0;
        const auto exit = enter(retired, 100000, lastStore);
        EXPECT_EQ(exit.reason, Core::Jit::ExitReason::LookupMiss);
        EXPECT_EQ(exit.pc, kCode + 8);
        EXPECT_EQ(cpu->m_gpr[3], count * 3);
        EXPECT_EQ(cpu->m_ctr, 0u);
        EXPECT_EQ(retired, count * 2);
    }
}

#endif
