#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ANDIS (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ANDIS_Greater)
{
    cpu->m_gpr[3] = 65536;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 5;

    Core::Instruction::ANDIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 65536);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, ANDIS_Zero)
{
    cpu->m_gpr[3] = 4;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 4;

    Core::Instruction::ANDIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, ANDIS_Less)
{
    cpu->m_gpr[3] = -1;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = -1;

    Core::Instruction::ANDIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], -65536);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
