#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ADDE (OE=0, RC=0, CA=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDE_NoOE_NoRc_NoCarry)
{
    cpu->m_gpr[3] = 10;
    cpu->m_gpr[4] = 32;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 42u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDE (OE=0, RC=0, CA=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDE_NoOE_NoRc_WithCarry)
{
    cpu->m_gpr[3] = 10;
    cpu->m_gpr[4] = 32;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 43u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDE. (OE=0, RC=1, CA=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDE_WithRc_UpdateCR)
{
    cpu->m_gpr[3] = -5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 8;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDE(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[8], 5);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────
//  ADDEO (OE=1, RC=0, CA=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDEO_WithOverflow_SetsOVandSO)
{
    cpu->m_gpr[3] = 0x7FFFFFFF;
    cpu->m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 2;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[2], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDEO. (OE=1, RC=1, CA=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDEO_WithOE_And_Rc)
{
    cpu->m_gpr[3] = 0x7FFFFFFF;
    cpu->m_gpr[4] = 0x7FFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 9;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDE(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);

    if (cpu->m_gprSigned[9] < 0) {
        EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
        EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
        EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    }
}
