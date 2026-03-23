#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  MULLW (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULLW_NoOE_NoRc_Positive)
{
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_NoOE_NoRc_Zero)
{
    cpu->m_gprSigned[3] = 0;
    cpu->m_gprSigned[4] = 12345;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_NoOE_NoRc_Negative)
{
    cpu->m_gprSigned[3] = -5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], -50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_NoOE_NoRc_BothNegative)
{
    cpu->m_gprSigned[3] = -5;
    cpu->m_gprSigned[4] = -10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  MULLW. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULLW_NoOE_WithRc_Positive)
{
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_NoOE_WithRc_Negative)
{
    cpu->m_gprSigned[3] = -5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], -50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_NoOE_WithRc_Zero)
{
    cpu->m_gprSigned[3] = 0;
    cpu->m_gprSigned[4] = 12345;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, MULLW_NoOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}

//
// ───────────────────────────────────────────────
//  MULLWO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULLW_WithOE_NoRc_NoOverflow)
{
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_WithOE_NoRc_PositiveOverflow)
{
    cpu->m_gprSigned[3] = INT32_MAX;
    cpu->m_gprSigned[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    // INT32_MAX * 2 = 0x1FFFFFFFE, low 32 bits = 0xFFFFFFFE
    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFEu);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_WithOE_NoRc_NegativeOverflow)
{
    cpu->m_gprSigned[3] = INT32_MIN;
    cpu->m_gprSigned[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    // INT32_MIN * 2 = 0xFFFFFFFF00000000, low 32 bits = 0x00000000
    EXPECT_EQ(cpu->m_gpr[5], 0u);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLW_WithOE_NoRc_SOPersists)
{
    cpu->m_xer.so = 1;
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  MULLWO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULLW_WithOE_WithRc_NoOverflow)
{
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 0);
}

TEST_F(InstructionTest, MULLW_WithOE_WithRc_PositiveOverflow)
{
    cpu->m_gprSigned[3] = INT32_MAX;
    cpu->m_gprSigned[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFEu);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}

TEST_F(InstructionTest, MULLW_WithOE_WithRc_NegativeOverflow)
{
    cpu->m_gprSigned[3] = INT32_MIN;
    cpu->m_gprSigned[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0u);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}

TEST_F(InstructionTest, MULLW_WithOE_WithRc_SOReflectedInCR0)
{
    cpu->m_xer.so = 1;
    cpu->m_gprSigned[3] = 5;
    cpu->m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::MULLW(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[5], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}
