// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_rlwimi
// */

#include "TestFixture.hpp"

// RLWIMI: rA = (ROTL32(rS, SH) & MASK(MB,ME)) | (rA & ~MASK(MB,ME))
// Unlike RLWNM, bits of rA OUTSIDE the mask are preserved.
// SH at inst.rb, MB at bits [6:10], ME at bits [1:5].

static uint32_t rotl32_w(uint32_t val, uint32_t sh)
{
    sh &= 31;
    return (sh == 0) ? val : ((val << sh) | (val >> (32 - sh)));
}

static uint32_t ppcMask_w(uint32_t mb, uint32_t me)
{
    if (mb <= me)
        return ((0xFFFFFFFFu >> mb) & (0xFFFFFFFFu << (31 - me)));
    return ~ppcMask_w(me + 1, mb - 1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — basic: insert rotated rS bits into rA at mask positions
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_BasicInsert)
{
    cpu->m_gpr[3] = 0xFF000000; // RS
    cpu->m_gpr[4] = 0x00000000; // RA (destination, starts zeroed)

    EncodedInstruction inst(0);
    inst.rt = 3; // RS
    inst.ra = 4; // RA
    inst.rb = 8; // SH=8 → ROTL(0xFF000000, 8) = 0x000000FF
    // MB=24, ME=31 → mask covers bits [24:31] = low byte
    inst.raw |= (24 << 6) | (31 << 1);

    Core::Instruction::RLWIMI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000FFu); // low byte inserted
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — full mask: rA is completely replaced by rotated rS
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_FullMask_RAFullyReplaced)
{
    cpu->m_gpr[3] = 0x12345678; // RS
    cpu->m_gpr[4] = 0xDEADBEEF; // RA (should be fully overwritten)

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 0; // SH=0 (no rotation)
    inst.raw |= (0 << 6) | (31 << 1); // MB=0, ME=31 (full mask)

    Core::Instruction::RLWIMI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x12345678u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — bits outside the mask in rA are preserved
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_PreservesRAOutsideMask)
{
    cpu->m_gpr[3] = 0xFFFFFFFF; // RS: all ones
    cpu->m_gpr[4] = 0xABCD1234; // RA: known pattern

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 0; // SH=0
    // MB=8, ME=15 → mask covers only bits [8:15]
    const uint32_t mask = ppcMask_w(8, 15);
    inst.raw |= (8 << 6) | (15 << 1);

    Core::Instruction::RLWIMI(*cpu, inst);

    // Inside mask: all 1s from RS
    // Outside mask: original rA bits
    uint32_t expected = (0xFFFFFFFFu & mask) | (0xABCD1234u & ~mask);
    EXPECT_EQ(cpu->m_gpr[4], expected);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — SH=0, insert low byte of rS into high byte of rA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_SH0_InsertLowByteIntoHighByte)
{
    cpu->m_gpr[3] = 0x000000AB; // RS: low byte = 0xAB
    cpu->m_gpr[4] = 0x00CDEF12; // RA

    // ROTL(RS, 24) = 0xAB000000; mask MB=0,ME=7 → insert into high byte of RA
    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 24; // SH=24
    inst.raw |= (0 << 6) | (7 << 1); // MB=0, ME=7

    Core::Instruction::RLWIMI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xABCDEF12u); // high byte replaced, rest preserved
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — RS not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xCAFEBABE; // RS
    cpu->m_gpr[4] = 0x00000000; // RA

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 8;
    inst.raw |= (0 << 6) | (31 << 1);

    Core::Instruction::RLWIMI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xCAFEBABEu); // RS unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — RC=1 updates CR0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_RC_UpdatesCR0)
{
    cpu->m_gpr[3] = 0x00000000; // RS = 0
    cpu->m_gpr[4] = 0x00000000; // RA = 0

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 0;
    inst.raw |= (0 << 6) | (31 << 1);
    inst.rc = 1;

    Core::Instruction::RLWIMI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWIMI — result is the updated rA (not the rotated rS)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWIMI_ResultIsUpdatedRA)
{
    cpu->m_gpr[3] = 0x000000FF; // RS
    cpu->m_gpr[4] = 0xAA000000; // RA: high byte already set

    // Rotate RS left by 0, mask MB=24,ME=31 (low byte)
    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 0; // SH=0
    inst.raw |= (24 << 6) | (31 << 1);

    Core::Instruction::RLWIMI(*cpu, inst);

    // Low byte from RS inserted; high byte of RA preserved
    EXPECT_EQ(cpu->m_gpr[4], 0xAA0000FFu);
}
