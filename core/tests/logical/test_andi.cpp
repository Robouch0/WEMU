#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ANDI (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ANDI_Greater)
{
    cpu->m_gpr[3] = 3;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 5;

    Core::Instruction::ANDI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0);
}

TEST_F(InstructionTest, ANDI_Zero)
{
    cpu->m_gpr[3] = 3;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 4;

    Core::Instruction::ANDI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1);
}

// as ui is a 16 bit unsigned int, negative result is impossible
