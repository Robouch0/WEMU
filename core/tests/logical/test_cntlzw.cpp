#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  CNTLZW (RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, CNTLZW_NoRc_AllZeros)
{
    cpu->m_gpr[3] = 0x00000000; // 32 zéros en tête

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 32);
}

TEST_F(InstructionTest, CNTLZW_NoRc_MSBSet)
{
    cpu->m_gpr[3] = 0x80000000; // bit 31 à 1 → 0 zéro en tête

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
}

TEST_F(InstructionTest, CNTLZW_NoRc_AllOnes)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
}

TEST_F(InstructionTest, CNTLZW_NoRc_OneLeadingZero)
{
    cpu->m_gpr[3] = 0x40000000; // 0b0100... → 1 zéro en tête

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1);
}

TEST_F(InstructionTest, CNTLZW_NoRc_MiddleValue)
{
    cpu->m_gpr[3] = 0x00100000; // bit 20 à 1 → 11 zéros en tête

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 11);
}

//
// ───────────────────────────────────────────────
//  CNTLZW. (RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, CNTLZW_WithRc_Greater)
{
    cpu->m_gpr[3] = 0x00100000; // résultat = 11, positif non nul

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 11);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, CNTLZW_WithRc_Zero)
{
    cpu->m_gpr[3] = 0xFFFFFFFF; // résultat = 0

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::CNTLZW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}