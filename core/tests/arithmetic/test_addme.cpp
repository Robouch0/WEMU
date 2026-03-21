#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  ADDME (OE=0, RC=0, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_NoOE_NoRc_NoCarry)
{
    cpu->m_gpr[3] = 10;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 9u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME (OE=0, RC=0, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_NoOE_NoRc_WithCarry)
{
    cpu->m_gpr[3] = 10;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 10u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME. (OE=0, RC=1, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_NoOe_WithRc_NoCarry)
{
    cpu->m_gpr[3] = 52;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 51u);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME. (OE=0, RC=1, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_NoOe_WithRc_WithCarry)
{
    cpu->m_gpr[3] = 52;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 52u);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO (OE=1, RC=0, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_WithOe_NoRc_NoCarry)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO (OE=1, RC=0, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_WithOe_NoRc_WithCarry)
{
    cpu->m_gpr[3] = 1;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO. (OE=1, RC=1, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_WithOe_WithRc_NoCarry)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO. (OE=1, RC=1, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDME_WithOe_WithRc_WithCarry)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDME(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}
