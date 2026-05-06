#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ANDC (RC=0)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ANDC_NoRc)
{
    cpu->m_gpr[3] = 3;
    cpu->m_gpr[4] = -2;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;

    Core::Instruction::ANDC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
}

//
// ───────────────────────────────────────────────
//  ANDC. (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ANDC_WithRc_Greater)
{
    cpu->m_gpr[3] = 3;
    cpu->m_gpr[4] = -2;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::ANDC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, ANDC_WithRc_Zero)
{
    cpu->m_gpr[3] = 3;
    cpu->m_gpr[4] = -1;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::ANDC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}

TEST_F(InstructionTest, ANDC_WithRc_Less)
{
    cpu->m_gpr[3] = -1;
    cpu->m_gpr[4] = 0;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::ANDC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], -1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}
