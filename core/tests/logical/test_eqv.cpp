#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  EQV
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, EQV)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = -9;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;

    Core::Instruction::EQV(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 13);
}


//
// ───────────────────────────────────────────────
//  EQV (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, EQV_WithRc_Greater)
{
    cpu->m_gpr[3] = -1;
    cpu->m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::EQV(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, EQV_WithRc_Zero)
{
    cpu->m_gpr[3] = -1;
    cpu->m_gpr[4] = 0;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::EQV(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, EQV_WithRc_Less)
{
    cpu->m_gpr[3] = 0;
    cpu->m_gpr[4] = 0;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::EQV(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], -1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
