#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  SUBFZE (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFZE_NoOE_NoRc_NoCarry)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0xFFFFFFFF + 0 = 0
    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_NoOE_NoRc_WithCarry)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0xFFFFFFFF + 1 = 1
    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_NoOE_NoRc_CarryOut)
{
    cpu->m_gpr[3] = 0;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0x00000000 + 1 = 0xFFFFFFFF + 1 = 0x100000000 -> carry out, result = 0
    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_NoOE_NoRc_NoCarryOut)
{
    cpu->m_gpr[3] = 0;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0x00000000 + 0 = 0xFFFFFFFF
    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  SUBFZE. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFZE_NoOE_WithRc_Positive)
{
    cpu->m_gpr[3] = 0xFFFFFFFE;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0xFFFFFFFE + 0 = 1
    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_NoOE_WithRc_Negative)
{
    cpu->m_gpr[3] = 0;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0x00000000 + 0 = 0xFFFFFFFF = -1
    EXPECT_EQ(cpu->m_gprSigned[4], -1);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_NoOE_WithRc_Zero)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0xFFFFFFFF + 0 = 0
    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, SUBFZE_NoOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0xFFFFFFFE;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}

//
// ───────────────────────────────────────────────
//  SUBFZEO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFZE_WithOE_NoRc_NoOverflow)
{
    cpu->m_gpr[3] = 0xFFFFFFFE;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    // ~0xFFFFFFFE + 0 = 1
    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_WithOE_NoRc_Overflow)
{
    // ~INT32_MIN + 1 = 0x7FFFFFFF + 1 = 0x80000000 -> overflow
    cpu->m_gprSigned[3] = INT32_MIN;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, SUBFZE_WithOE_NoRc_SOPersists)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0xFFFFFFFE;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  SUBFZEO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFZE_WithOE_WithRc_NoOverflow)
{
    cpu->m_gpr[3] = 0xFFFFFFFE;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 0);
}

TEST_F(InstructionTest, SUBFZE_WithOE_WithRc_Overflow)
{
    cpu->m_gprSigned[3] = INT32_MIN;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}

TEST_F(InstructionTest, SUBFZE_WithOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0xFFFFFFFE;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFZE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}
