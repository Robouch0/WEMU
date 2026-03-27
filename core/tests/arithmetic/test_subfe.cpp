#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  SUBFE: RB=100, RA=42, CA=1, OE=0, RC=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_SimpleWithCarry)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE: RB=100, RA=42, CA=0, OE=0, RC=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_SimpleNoCarry)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;
    cpu->m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 57u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE: RB=10, RA=100, CA=1, OE=0, RC=0, negative result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_NegativeResult)
{
    cpu->m_gpr[3] = 100;
    cpu->m_gpr[5] = 10;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -90);
    EXPECT_EQ(cpu->m_xer.ca, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE: RB=42, RA=42, CA=1, OE=0, RC=0, zero result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_ZeroResult)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 42;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE: RB=0xFFFFFFFF, RA=0, CA=1, OE=0, RC=0, carry out
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_CarryOut)
{
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[5] = 0xFFFFFFFF;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE.: RB=100, RA=42, CA=1, OE=0, RC=1, positive result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithRc_Positive)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE.: RB=10, RA=100, CA=1, OE=0, RC=1, negative result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithRc_Negative)
{
    cpu->m_gpr[3] = 100;
    cpu->m_gpr[5] = 10;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -90);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFE.: RB=42, RA=42, CA=1, OE=0, RC=1, zero result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithRc_Zero)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 42;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFEO: RB=0x80000000, RA=1, CA=1, OE=1, RC=0, overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithOE_Overflow)
{
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[5] = 0x80000000;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFEO: RB=100, RA=42, CA=1, OE=1, RC=0, no overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithOE_NoOverflow)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFEO: OE=1, RC=0, SO persists
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithOE_SOPersists)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFEO.: RB=0x80000000, RA=1, CA=1, OE=1, RC=1, overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFE_WithOE_WithRc_Overflow)
{
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[5] = 0x80000000;
    cpu->m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBFE(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1);
}
