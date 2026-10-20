/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_x64_jit_memory -- integer loads and stores against the interpreter
*/

#include "jit/JitHarness.hpp"

#ifdef WEMU_HAS_X64_JIT

namespace {
    struct Form {
            unsigned code; // primary opcode, or the extended opcode when indexed
            bool indexed, update;
    };
    // Loads then stores, D-form and X-form (with update variants).
    constexpr Form kLoads[] = {{34, false, false}, {35, false, true}, {40, false, false}, {41, false, true}, {42, false, false}, {43, false, true},
                               {32, false, false}, {33, false, true}, {87, true, false},  {119, true, true}, {279, true, false}, {311, true, true},
                               {343, true, false}, {375, true, true}, {23, true, false},  {55, true, true},  {534, true, false}};
    constexpr Form kStores[] = {{38, false, false}, {39, false, true},  {44, false, false}, {45, false, true},  {36, false, false},
                                {37, false, true},  {215, true, false}, {247, true, true},  {407, true, false}, {439, true, true},
                                {151, true, false}, {183, true, true},  {662, true, false}};
} // namespace

class X64JitMemoryTest : public X64JitTest {
    protected:
        // Picks an effective address in one of the interesting places.
        std::uint32_t target(Random &rng, const unsigned kind)
        {
            switch (kind) {
                case 0:
                    return kData + rng() % (kDataBytes - 8); // plain RAM, any alignment
                case 1:
                    return kData + 0x1000 - 1 - rng() % 3; // crosses a page
                case 2:
                    return kStack + rng() % (kWindowBytes - 8); // stack region
                case 3:
                    return kCodeData + rng() % 0x100; // a page holding decoded code
                default:
                    return 0x1000 + rng() % 0x100; // unmapped
            }
        }

        // Encodes `form` with random registers and makes its effective address `ea`.
        std::uint32_t encode(Random &rng, const Form &form, std::uint32_t &ra, std::uint32_t &rb)
        {
            ra = 1 + rng() % 31; // RA=0 is covered by dedicated cases
            rb = reg(rng);
            if (form.indexed)
                return xForm(form.code, reg(rng), ra, rb);
            return dForm(form.code, reg(rng), ra, value(rng));
        }

        void point(const Form &form, const std::uint32_t word, const std::uint32_t ra, const std::uint32_t rb, const std::uint32_t ea, Random &rng)
        {
            if (form.indexed) {
                if (ra == rb) {
                    cpu->m_gpr[ra] = ea / 2;
                } else {
                    cpu->m_gpr[ra] = rng();
                    cpu->m_gpr[rb] = ea - cpu->m_gpr[ra];
                }
            } else {
                cpu->m_gpr[ra] = ea - static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xFFFF)));
            }
        }

        void run(const Form &form, Random &rng, const unsigned kind)
        {
            std::uint32_t ra{}, rb{};
            const std::uint32_t word = encode(rng, form, ra, rb);
            std::uint32_t ea = target(rng, kind);
            if (form.indexed && ra == rb)
                ea &= ~1u;
            expectSameBlock(std::array{word}, [&] {
                randomize(rng);
                point(form, word, ra, rb, ea, rng);
            });
        }
};

TEST_F(X64JitMemoryTest, LoadsMatchInterpreter)
{
    Random rng(20);
    for (int round = 0; round < 40; ++round) {
        for (unsigned i = 0; i < kDataBytes; i += 4)
            cpu->m_memory.write<std::uint32_t>(kData + i, rng());
        for (unsigned i = 0; i < kWindowBytes; i += 4)
            cpu->m_memory.write<std::uint32_t>(kStack + i, rng());
        for (const auto &form: kLoads)
            for (unsigned kind = 0; kind < 5; ++kind)
                run(form, rng, kind);
    }
}

TEST_F(X64JitMemoryTest, StoresMatchInterpreter)
{
    Random rng(21);
    for (int round = 0; round < 40; ++round)
        for (const auto &form: kStores)
            for (unsigned kind = 0; kind < 5; ++kind)
                run(form, rng, kind);
}

TEST_F(X64JitMemoryTest, ZeroBaseRegisterUsesZero)
{
    Random rng(22);
    for (int round = 0; round < 64; ++round) {
        // lwz r3,d(0) and stw r3,d(0): low addresses are unmapped
        const std::uint32_t d = rng() & 0x7FFC;
        expectSameBlock(std::array{dForm(32, 3, 0, d)}, [&] { randomize(rng); });
        expectSameBlock(std::array{dForm(36, 3, 0, d)}, [&] { randomize(rng); });
        // lwzx r3,0,r4 / stwx r3,0,r4
        const std::uint32_t ea = kData + (rng() & 0xFFC);
        expectSameBlock(std::array{xForm(23, 3, 0, 4)}, [&] {
            randomize(rng);
            cpu->m_gpr[4] = ea;
        });
        expectSameBlock(std::array{xForm(151, 3, 0, 4)}, [&] {
            randomize(rng);
            cpu->m_gpr[4] = ea;
        });
    }
}

TEST_F(X64JitMemoryTest, MultipleLoadsAndStoresMatchInterpreter)
{
    Random rng(24);
    for (int round = 0; round < 64; ++round) {
        for (unsigned i = 0; i < kDataBytes; i += 4)
            cpu->m_memory.write<std::uint32_t>(kData + i, rng());
        for (const unsigned op: {46u, 47u}) // lmw, stmw
            for (unsigned kind = 0; kind < 5; ++kind) {
                const std::uint32_t rt = rng() & 31, ra = 1 + rng() % 31;
                const std::uint32_t word = dForm(op, rt, ra, rng() & 0xFFFC);
                const std::uint32_t ea = kind == 1 ? kData + 0x1000 - 8 : target(rng, kind) & ~3u; // kind 1: across a page
                expectSameBlock(std::array{word}, [&] {
                    randomize(rng);
                    cpu->m_gpr[ra] = ea - static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xFFFF)));
                });
            }
    }
}

TEST_F(X64JitMemoryTest, WatchedPagesCountCompiledStores)
{
    cpu->m_memory.watchWrites(kData + 0x2000, 0x1000);
    const auto before = cpu->m_memory.writeGeneration(kData + 0x2000, 0x1000);
    ASSERT_TRUE(before.has_value());
    const std::array<std::uint32_t, 3> words{0x90640000, 0x90640004, 0x48000004}; // stw r3,0(r4) ; stw r3,4(r4) ; b +4
    load(words);
    cpu->m_pc = kCode;
    cpu->m_gpr[3] = 0x12345678;
    cpu->m_gpr[4] = kData + 0x2100;
    std::uint64_t retired = 0, lastStore = 0;
    enter(retired, 1000, lastStore);
    EXPECT_EQ(cpu->m_memory.read<std::uint32_t>(kData + 0x2104), 0x12345678u);
    EXPECT_EQ(*cpu->m_memory.writeGeneration(kData + 0x2000, 0x1000), *before + 2);
}

TEST_F(X64JitMemoryTest, MixedMemoryBlocksMatchInterpreter)
{
    Random rng(23);
    for (int round = 0; round < 200; ++round) {
        // r30/r31 point into scratch and stack; random loads, stores and adds around them.
        std::vector<std::uint32_t> words;
        for (int n = 0; n < 16; ++n) {
            const unsigned base = 30 + (rng() & 1);
            const std::uint32_t d = rng() & 0x3FC;
            switch (rng() % 5) {
                case 0:
                    words.push_back(dForm(32 + 2 * (rng() % 6), reg(rng) & 15, base, d)); // lwz lbz lhz lha ... (no update)
                    break;
                case 1:
                    words.push_back(dForm(36 + 2 * (rng() % 5), reg(rng) & 15, base, d)); // stw stb sth
                    break;
                case 2:
                    words.push_back(dForm(14, reg(rng) & 15, reg(rng) & 15, value(rng)));
                    break;
                case 3:
                    words.push_back(xForm(266, reg(rng) & 15, reg(rng) & 15, reg(rng) & 15, rng() & 1));
                    break;
                default:
                    words.push_back(dForm(37, reg(rng) & 15, base, d & 0x3C)); // stwu
                    break;
            }
        }
        expectSameBlock(words, [&] {
            randomize(rng);
            cpu->m_gpr[30] = kData + 0x800;
            cpu->m_gpr[31] = kStack + 0x400;
        });
    }
}

#endif
