#include "TestFixture.hpp"

// TODO: add ADDIC / ADDIC. tests
//
// ───────────────────────────────────────────────────────────────
//  ADDIC. OPCODE 13
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDIC__PositiveResult)
{
    cpu->m_gpr[3] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 50;

    Core::Instruction::ADDIC_(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 150u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, ADDIC__NegativeResult)
{
    cpu->m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = -1;

    Core::Instruction::ADDIC_(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, ADDIC__ZeroResult)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 1;

    Core::Instruction::ADDIC_(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, ADDIC__WithCarry)
{
    cpu->m_gpr[3] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = -50;

    Core::Instruction::ADDIC_(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 50u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
