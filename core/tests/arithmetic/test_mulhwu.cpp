#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  MULHWU (RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULHWU_NoRc_LowProduct)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
}

TEST_F(InstructionTest, MULHWU_NoRc_HighProduct)
{
    cpu->m_gpr[3] = 45987;
    cpu->m_gpr[4] = 653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
}

//
// ───────────────────────────────────────────────
//  MULHWU (RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULHWU_WithRc_LowProduct)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, MULHWU_WithRc_HighProduct)
{
    cpu->m_gpr[3] = 45987;
    cpu->m_gpr[4] = 653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULHWU_WithRc_NegativeRC)
{
    cpu->m_gpr[3] = 0x890F3A33;
    cpu->m_gpr[4] = 0xF098D968;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x80D01BFF);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}