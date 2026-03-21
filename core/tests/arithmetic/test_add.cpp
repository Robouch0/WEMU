#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ADD (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADD_NoOE_NoRc)
{
    cpu->m_gpr[3] = 10;
    cpu->m_gpr[4] = 32;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADD(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 42u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADD. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADD_WithRc_UpdateCR)
{
    cpu->m_gpr[3] = -5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 8;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADD(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[8], 5);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────
//  ADDO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDO_WithOverflow_SetsOVandSO)
{
    cpu->m_gpr[3] = 0x7FFFFFFF;
    cpu->m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 2;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADD(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[2], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDO_WithOE_And_Rc)
{
    cpu->m_gpr[3] = 0x7FFFFFFF;
    cpu->m_gpr[4] = 0x7FFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 9;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADD(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[9], 0x7FFFFFFF + 0x7FFFFFFF);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);

    if (cpu->m_gprSigned[9] < 0) {
        EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
        EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
        EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    }
}
