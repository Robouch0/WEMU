/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_rlwnm
*/

#include "TestFixture.hpp"

// RLWNM: rA = ROTL32(rS, rB[27:31]) & MASK(MB, ME)
// Fields: inst.rt=RS, inst.ra=RA(dest), inst.rb=RB(rotation amount register)
// MB at C++ raw bits [6:10]: inst.raw |= (mb << 6)
// ME at C++ raw bits [1:5]:  inst.raw |= (me << 1)

static uint32_t rotl32(uint32_t val, uint32_t sh)
{
    sh &= 31;
    return (sh == 0) ? val : ((val << sh) | (val >> (32 - sh)));
}

static uint32_t ppcMask(uint32_t mb, uint32_t me)
{
    if (mb <= me)
        return ((0xFFFFFFFFu >> mb) & (0xFFFFFFFFu << (31 - me)));
    return ~ppcMask(me + 1, mb - 1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — basic rotate and mask
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_BasicRotateAndMask)
{
    cpu->m_gpr[3] = 0xFF000000; // high byte set
    cpu->m_gpr[5] = 8;          // rotate left by 8

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    // MB=0, ME=31 → full mask (no masking effect)
    inst.raw |= (0 << 6) | (31 << 1);

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00FF0000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — rotate by 0: only mask applied
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_RotateBy0)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 0; // no rotation

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    // MB=8, ME=15 → keep only bits [8:15] (second byte)
    inst.raw |= (8 << 6) | (15 << 1);

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], ppcMask(8, 15)); // 0x00FF0000
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — full mask (MB=0, ME=31): result is just the rotation
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_FullMask)
{
    cpu->m_gpr[3] = 0x12345678;
    cpu->m_gpr[5] = 8;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.raw |= (0 << 6) | (31 << 1); // MB=0, ME=31

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], rotl32(0x12345678, 8));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — rotate by 31 (= rotate right by 1)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_RotateBy31)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 31;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.raw |= (0 << 6) | (31 << 1); // full mask

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u); // rotated left by 31 = right by 1
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — extract a single bit (MB=ME)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_ExtractSingleBit)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 0; // no rotation

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    // MB=31, ME=31 → keep only bit 31 (LSB in PPC = bit 31)
    inst.raw |= (31 << 6) | (31 << 1);

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — zero result when mask clears rotated bits
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_ZeroResult)
{
    cpu->m_gpr[3] = 0xFF000000; // high byte only
    cpu->m_gpr[5] = 0; // no rotation

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    // MB=24, ME=31 → keep only bits [24:31] (low byte); high byte is gone
    inst.raw |= (24 << 6) | (31 << 1);

    Core::Instruction::RLWNM(*cpu, inst);

    // 0xFF000000 & MASK(24,31) = 0x00000000 — the high byte is outside [24:31]
    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — RC=1 updates CR0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_RC_UpdatesCR0)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 31; // rotates 1 → 0x80000000

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.raw |= (0 << 6) | (31 << 1);
    inst.rc = 1;

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1u); // result is negative
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0u);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWNM — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWNM_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xDEADBEEF;
    cpu->m_gpr[5] = 4;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.raw |= (0 << 6) | (31 << 1);

    Core::Instruction::RLWNM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xDEADBEEFu);
}
