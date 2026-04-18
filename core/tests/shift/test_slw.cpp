/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_slw
*/

#include "TestFixture.hpp"

// SLW: rA = rS << rB[27:31]  (shift amount = low 5 bits of rB)
// Fields: inst.rt=RS, inst.ra=RA(dest), inst.rb=RB(shift amount register)

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — basic shift left by 1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_BasicShift)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 1; // shift amount

    EncodedInstruction inst(0);
    inst.rt = 3; // RS
    inst.ra = 4; // RA (dest)
    inst.rb = 5; // RB

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000002u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — shift by 0 leaves value unchanged
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftBy0)
{
    cpu->m_gpr[3] = 0xDEADBEEF;
    cpu->m_gpr[5] = 0;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — shift by 31 moves LSB to MSB
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftBy31)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 31;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — high bits are discarded when shifted out
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_HighBitsDiscarded)
{
    cpu->m_gpr[3] = 0x80000001; // MSB set
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000002u); // MSB shifted out
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — shift by 16
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftBy16)
{
    cpu->m_gpr[3] = 0x0000FFFF;
    cpu->m_gpr[5] = 16;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF0000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — shift 0x00000002 by 31: only bit 1 set, shifted to MSB → 0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftProducesZero)
{
    cpu->m_gpr[3] = 0x00000002; // bit 1 set
    cpu->m_gpr[5] = 31;         // << 31 → bit 1 goes past bit 31, result = 0

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — max positive SI: large valid shift succeeds (EXPECT_NO_THROW)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftBy30_OnlyTwoBitsRemain)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 30;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    EXPECT_NO_THROW(Core::Instruction::SLW(*cpu, inst));
    EXPECT_EQ(cpu->m_gpr[4], 0xC0000000u); // only top 2 bits survive
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — RC=1 updates CR0: result is positive
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_RC_ResultPositive)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.rc = 1;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 2u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — RC=1 updates CR0: result is negative (high bit set)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_RC_ResultNegative)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 31;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.rc = 1;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1u); // high bit set → negative
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xCAFEBABE;
    cpu->m_gpr[5] = 4;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xCAFEBABEu); // RS unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — shift >= 32 produces zero (bit 26 BigEndian of RB is set)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftBy32_ProducesZero)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 32; // bit 5 set → shift >= 32 → result = 0

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SLW — shift by 63 produces zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SLW_ShiftBy63_ProducesZero)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 63;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}
