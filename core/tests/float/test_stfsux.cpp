/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_stfsux
*/

#include <bit>

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  STFSUX — stores FRS as single precision at RA+RB and updates RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STFSUX_StoresSingleAndUpdatesRA)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 8;
    cpu->m_fpr[3] = 1.5;

    EncodedInstruction inst(0);
    inst.frt = 3;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STFSUX(*cpu, inst);

    const uint32_t raw = cpu->m_memory.read<uint32_t>(TEST_ADDR + 8);
    EXPECT_EQ(std::bit_cast<float>(raw), 1.5f);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 8);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STFSUX — double value is narrowed to float on store
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STFSUX_NarrowsDoubleToFloat)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;
    cpu->m_fpr[3] = 0.1; // not exactly representable; narrows to 0.1f

    EncodedInstruction inst(0);
    inst.frt = 3;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STFSUX(*cpu, inst);

    const uint32_t raw = cpu->m_memory.read<uint32_t>(TEST_ADDR);
    EXPECT_EQ(std::bit_cast<float>(raw), 0.1f);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}
