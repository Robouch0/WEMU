#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  XOR (RC=0)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, XOR_NoRc)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 9;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;

    Core::Instruction::XOR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 12);
}

//
// ───────────────────────────────────────────────
//  XOR (RC=1)
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, XOR_WithRc_Greater)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 9;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::XOR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 12);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, XOR_WithRc_Zero)
{
    cpu->m_gpr[3] = 2;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::XOR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, XOR_WithRc_Less)
{
    cpu->m_gpr[3] = 1;
    cpu->m_gpr[4] = -1;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;
    inst.rc = 1;

    Core::Instruction::XOR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4294967294);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
