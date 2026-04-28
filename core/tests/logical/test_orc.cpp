#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ORC (RC=0)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ORC_NoRc)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 3;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;

    Core::Instruction::ORC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4294967293);
}

//
// ───────────────────────────────────────────────
//  ORC. (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ORC_WithRc_Greater)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = -1;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::ORC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 5);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, ORC_WithRc_Zero)
{
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = -1;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::ORC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}


TEST_F(InstructionTest, ORC_WithRc_Less)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 3;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::ORC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4294967293);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}
