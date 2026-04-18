#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  NEG (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, NEG_NoOE_NoRc_Positive)
{
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_NoOE_NoRc_Negative)
{
    cpu->m_gprSigned[3] = -5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_NoOE_NoRc_Zero)
{
    cpu->m_gprSigned[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────
//  NEG. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, NEG_NoOE_WithRc_Positive)
{
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_NoOE_WithRc_Negative)
{
    cpu->m_gprSigned[3] = -5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_NoOE_WithRc_Zero)
{
    cpu->m_gprSigned[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}

TEST_F(InstructionTest, NEG_NoOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}

//
// ───────────────────────────────────────────────
//  NEGO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, NEG_WithOE_NoRc_NoOverflow)
{
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_WithOE_NoRc_Overflow)
{
    cpu->m_gprSigned[3] = INT32_MIN;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], INT32_MIN);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_WithOE_NoRc_SOPersists)
{
    cpu->m_xer.so = 1;
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────
//  NEGO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, NEG_WithOE_WithRc_NoOverflow)
{
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0);
}

TEST_F(InstructionTest, NEG_WithOE_WithRc_Overflow)
{
    cpu->m_gprSigned[3] = INT32_MIN;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], INT32_MIN);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}

TEST_F(InstructionTest, NEG_WithOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::NEG(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -5);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}
