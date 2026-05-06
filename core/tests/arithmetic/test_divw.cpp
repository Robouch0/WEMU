#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  DIVW (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, DIVW_NoOe_NoRc_IntegerResult)
{
    cpu->m_gpr[3] = 10;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;
    inst.oe = 0;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 5);
}

TEST_F(InstructionTest, DIVW_NoOe_NoRc_FloatResult)
{
    cpu->m_gpr[3] = 9;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;
    inst.oe = 0;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4);
}

//
// ───────────────────────────────────────────────
//  DIVW. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, DIVW_NoOe_WithRc_Greater)
{
    cpu->m_gpr[3] = 10;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;
    inst.oe = 0;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 5);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, DIVW_NoOe_WithRc_Zero)
{
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;
    inst.oe = 0;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}

TEST_F(InstructionTest, DIVW_NoOe_WithRc_Less)
{
    cpu->m_gpr[3] = -10;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;
    inst.oe = 0;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], -5);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────
//  DIVWO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, DIVW_WithOe_NoRc_NoError)
{
    cpu->m_gpr[3] = 50;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;
    inst.oe = 1;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 25);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

TEST_F(InstructionTest, DIVW_WithOe_NoRc_NoErrorBigNegative)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;
    inst.oe = 1;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xC0000000);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

TEST_F(InstructionTest, DIVW_WithOe_NoRc_ImpossibleDivisionByMinusOne)
{
    cpu->m_gpr[3] = 0x80000000;
    cpu->m_gpr[4] = -1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;
    inst.oe = 1;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
}

TEST_F(InstructionTest, DIVW_WithOe_NoRc_ImpossibleDivisionByZero)
{
    cpu->m_gpr[3] = 67;
    cpu->m_gpr[4] = 0;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;
    inst.oe = 1;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
}

//
// ───────────────────────────────────────────────
//  DIVWO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, DIVW_WithOe_WithRc_NoError)
{
    cpu->m_gpr[3] = 67;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;
    inst.oe = 1;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 33);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0);
}

TEST_F(InstructionTest, DIVW_WithOe_WithRc_ImpossibleDivisionByZero)
{
    cpu->m_gpr[3] = 67;
    cpu->m_gpr[4] = 0;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;
    inst.oe = 1;

    Core::Instruction::DIVW(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}