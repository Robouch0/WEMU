#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  AND (RC=0)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, AND_NoRc)
{
    cpu->m_gpr[3] = 3;
    cpu->m_gpr[4] = 5;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;

    Core::Instruction::AND(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
}

//
// ───────────────────────────────────────────────
//  AND. (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, AND_WithRc_Greater)
{
    cpu->m_gpr[3] = 3;
    cpu->m_gpr[4] = 5;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::AND(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, AND_WithRc_Zero)
{
    cpu->m_gpr[3] = 3;
    cpu->m_gpr[4] = 4;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::AND(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, AND_WithRc_Less)
{
    cpu->m_gpr[3] = -1;
    cpu->m_gpr[4] = -5;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::AND(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4294967291);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
