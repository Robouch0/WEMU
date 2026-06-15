#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  SUBF: RB=100, RA=42, OE=0, RC=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_SimplePositive)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBF: RB=42, RA=42, zero result, OE=0, RC=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_ZeroResult)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 42;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBF: RB=10, RA=100, negative result, OE=0, RC=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_NegativeResult)
{
    cpu->m_gpr[3] = 100;
    cpu->m_gpr[5] = 10;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -90);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBF.: RB=100, RA=42, OE=0, RC=1, positive result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithRc_Positive)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBF.: RB=10, RA=100, OE=0, RC=1, negative result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithRc_Negative)
{
    cpu->m_gpr[3] = 100;
    cpu->m_gpr[5] = 10;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -90);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBF.: RB=42, RA=42, OE=0, RC=1, zero result
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithRc_Zero)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 42;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFO: RB=0x80000000, RA=1, OE=1, RC=0, overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithOE_Overflow)
{
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[5] = 0x80000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFO: RB=100, RA=42, OE=1, RC=0, no overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithOE_NoOverflow)
{
    cpu->m_gpr[3] = 42;
    cpu->m_gpr[5] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFO.: RB=0x80000000, RA=1, OE=1, RC=1, overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithOE_WithRc_Overflow)
{
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[5] = 0x80000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x7FFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ov, 1);
    EXPECT_EQ(cpu->m_xer.so, 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFO: RB=50, RA=-50, OE=1, RC=0, same sign no overflow
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBF_WithOE_SameSignNoOverflow)
{
    cpu->m_gprSigned[3] = -50;
    cpu->m_gpr[5] = 50;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.rb = 5;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::SUBF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 100u);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
}
