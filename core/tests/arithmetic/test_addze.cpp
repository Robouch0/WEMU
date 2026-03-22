#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  ADDZE (OE=0, RC=0, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_NoOE_NoRc_NoCarry)
{
    cpu->m_gpr[3] = 42;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 42u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZE (OE=0, RC=0, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_NoOE_NoRc_WithCarry)
{
    cpu->m_gpr[3] = 42;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 43u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZE (OE=0, RC=0, CA=1, carry out)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_NoOE_NoRc_CarryOut)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZE. (OE=0, RC=1, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_NoOE_WithRc_Positive)
{
    cpu->m_gpr[3] = 10;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 10u);
    EXPECT_EQ(cpu->m_xer.ca, 0);

    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);

    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZE. (OE=0, RC=1, CA=1, negative result)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_NoOE_WithRc_Negative)
{
    cpu->m_gprSigned[3] = -5;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ca, 0);

    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);

    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZE. (OE=0, RC=1, CA=1, zero result)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_NoOE_WithRc_Zero)
{
    cpu->m_gprSigned[3] = -1;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);

    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);

    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZEO (OE=1, RC=0, CA=1, overflow)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_WithOE_NoRc_Overflow)
{
    cpu->m_gpr[3] = 0x7FFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZEO (OE=1, RC=0, CA=0, no overflow)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_WithOE_NoRc_NoOverflow)
{
    cpu->m_gpr[3] = 10;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 10u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZEO (OE=1, RC=0, SO persists)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_WithOE_NoRc_SOPersists)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 5;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 5u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZEO. (OE=1, RC=1, CA=1, overflow)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_WithOE_WithRc_Overflow)
{
    cpu->m_gpr[3] = 0x7FFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);

    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDZEO. (OE=1, RC=1, no overflow, SO reflected in CR0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDZE_WithOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 5;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 6u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);

    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}
