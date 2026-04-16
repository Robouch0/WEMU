#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  SUBFME (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFME_NoOE_NoRc_NoCarry)
{
    // ~0xFFFFFFFF + 0 + 0xFFFFFFFF = 0 + 0xFFFFFFFF = 0xFFFFFFFF, carry=0
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_NoOE_NoRc_WithCarry)
{
    // ~0xFFFFFFFF + 1 + 0xFFFFFFFF = 0x100000000 -> result=0, carry=1
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_NoOE_NoRc_CarryOut_NoCarryIn)
{
    // ~0x00000000 + 0 + 0xFFFFFFFF = 0xFFFFFFFF + 0xFFFFFFFF = 0x1FFFFFFFE -> result=0xFFFFFFFE, carry=1
    cpu->m_gpr[3] = 0;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFEu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_NoOE_NoRc_CarryOut_WithCarryIn)
{
    // ~0x00000000 + 1 + 0xFFFFFFFF = 0xFFFFFFFF + 1 + 0xFFFFFFFF = 0x1FFFFFFFF -> result=0xFFFFFFFF, carry=1
    cpu->m_gpr[3] = 0;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────
//  SUBFME. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFME_NoOE_WithRc_Positive)
{
    // ~0xFFFFFFFD + 0 + 0xFFFFFFFF = 2 + 0xFFFFFFFF = 0x100000001 -> result=1, carry=1
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_NoOE_WithRc_Negative)
{
    // ~0x00000000 + 0 + 0xFFFFFFFF = 0xFFFFFFFE = -2 (signed)
    cpu->m_gpr[3] = 0;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -2);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_NoOE_WithRc_Zero)
{
    // ~0xFFFFFFFE + 1 + 0xFFFFFFFF = 1 + 1 + 0xFFFFFFFF = 0x100000001 -> result=1...
    // Let's find zero: need ~RA + CA + 0xFFFFFFFF = 0x100000000 -> ~RA + CA = 1
    // ~RA=0, CA=1 -> RA=0xFFFFFFFF, CA=1
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    // ~0xFFFFFFFF + 1 + 0xFFFFFFFF = 0 + 1 + 0xFFFFFFFF = 0x100000000 -> result=0, carry=1
    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}

TEST_F(InstructionTest, SUBFME_NoOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    // ~0xFFFFFFFD + 0 + 0xFFFFFFFF = 2 + 0xFFFFFFFF = 0x100000001 -> result=1, carry=1
    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}

//
// ───────────────────────────────────────────────
//  SUBFMEO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFME_WithOE_NoRc_NoOverflow)
{
    // ~0xFFFFFFFD + 0 + 0xFFFFFFFF = 2 + 0xFFFFFFFF = 0x100000001 -> result=1, carry=1
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_WithOE_NoRc_Overflow)
{
    // RA=INT32_MAX -> ~RA=INT32_MIN, CA=0
    // INT32_MIN + 0 + 0xFFFFFFFF = 0x80000000 + 0xFFFFFFFF = 0x17FFFFFFF -> result=0x7FFFFFFF, carry=1
    // signed overflow: INT32_MIN + (-1) = INT32_MAX -> overflow
    cpu->m_gprSigned[3] = INT32_MAX;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_WithOE_NoRc_SOPersists)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────
//  SUBFMEO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFME_WithOE_WithRc_NoOverflow)
{
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0);
}

TEST_F(InstructionTest, SUBFME_WithOE_WithRc_Overflow)
{
    // RA=INT32_MAX -> ~RA=INT32_MIN, CA=0 -> result=INT32_MAX, overflow
    cpu->m_gprSigned[3] = INT32_MAX;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}

TEST_F(InstructionTest, SUBFME_WithOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}

//
// ───────────────────────────────────────────────
//  SUBFME — rt == ra aliasing
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFME_RtEqualsRa_NoCarry)
{
    // ~0xFFFFFFFD + 0 + 0xFFFFFFFF = 2 + 0xFFFFFFFF = 0x100000001 -> result=1, carry=1
    cpu->m_gpr[3] = 0xFFFFFFFD;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
}

TEST_F(InstructionTest, SUBFME_RtEqualsRa_OverflowCase)
{
    // RA=INT32_MAX -> ~RA=INT32_MIN, CA=0 -> result=INT32_MAX, overflow
    cpu->m_gprSigned[3] = INT32_MAX;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
}
