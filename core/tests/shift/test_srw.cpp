/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_srw
*/

#include "TestFixture.hpp"

// SRW: rA = rS >> rB[27:31]  (logical right shift, zero-fill)
// Fields: inst.rt=RS, inst.ra=RA(dest), inst.rb=RB(shift amount register)

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — basic shift right by 1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_BasicShift)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x40000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — shift by 0 leaves value unchanged
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_ShiftBy0)
{
    cpu->m_gpr[3] = 0xDEADBEEF;
    cpu->m_gpr[5] = 0;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — shift by 31 moves MSB to LSB
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_ShiftBy31)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_gpr[5] = 31;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — no sign extension: high bit cleared after shift
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_NoSignExtension)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu); // zero fill, not sign fill
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — shift by 16
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_ShiftBy16)
{
    cpu->m_gpr[3] = 0xFFFF0000;
    cpu->m_gpr[5] = 16;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000FFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — shift a value with only LSB set by 1 gives 0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_LSBOnlyShiftedOut)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — RC=1 updates CR0: result positive
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_RC_ResultPositive)
{
    cpu->m_gpr[3] = 0x00000010;
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.rc = 1;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000008u);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0u);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1u);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — RC=1 updates CR0: result is zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_RC_ResultZero)
{
    cpu->m_gpr[3] = 0x00000001;
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.rc = 1;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0u);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0u);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xCAFEBABE;
    cpu->m_gpr[5] = 4;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xCAFEBABEu); // RS unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — shift >= 32 produces zero (bit 26 BigEndian of RB is set)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_ShiftBy32_ProducesZero)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 32;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRW — shift by 63 produces zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRW_ShiftBy63_ProducesZero)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_gpr[5] = 63;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}
