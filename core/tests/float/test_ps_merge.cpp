/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_ps_merge
*/

#include "TestFixture.hpp"

namespace {
    EncodedInstruction mergeInst(const int dst, const int a, const int b)
    {
        EncodedInstruction inst(0);
        inst.frt = dst;
        inst.fra = a;
        inst.frb = b;
        return inst;
    }
} // namespace

TEST_F(InstructionTest, PS_MERGE10_SelfOverlapSwapsOriginalLanes)
{
    cpu->m_fpr[31] = 10.0;
    cpu->m_ps1[31] = 20.0;

    Core::Instruction::PS_MERGE10(*cpu, mergeInst(31, 31, 31));

    EXPECT_DOUBLE_EQ(cpu->m_fpr[31], 20.0);
    EXPECT_DOUBLE_EQ(cpu->m_ps1[31], 10.0);
}

TEST_F(InstructionTest, PS_MERGE00_DestinationMayOverlapFirstSource)
{
    cpu->m_fpr[3] = 1.0;
    cpu->m_ps1[3] = 2.0;
    cpu->m_fpr[4] = 3.0;
    cpu->m_ps1[4] = 4.0;

    Core::Instruction::PS_MERGE00(*cpu, mergeInst(3, 3, 4));

    EXPECT_DOUBLE_EQ(cpu->m_fpr[3], 1.0);
    EXPECT_DOUBLE_EQ(cpu->m_ps1[3], 3.0);
}

TEST_F(InstructionTest, PS_MERGE01_DestinationMayOverlapSecondSource)
{
    cpu->m_fpr[3] = 1.0;
    cpu->m_ps1[3] = 2.0;
    cpu->m_fpr[4] = 3.0;
    cpu->m_ps1[4] = 4.0;

    Core::Instruction::PS_MERGE01(*cpu, mergeInst(4, 3, 4));

    EXPECT_DOUBLE_EQ(cpu->m_fpr[4], 1.0);
    EXPECT_DOUBLE_EQ(cpu->m_ps1[4], 4.0);
}

TEST_F(InstructionTest, PS_MERGE11_SelfOverlapDuplicatesOriginalPs1)
{
    cpu->m_fpr[5] = 7.0;
    cpu->m_ps1[5] = 8.0;

    Core::Instruction::PS_MERGE11(*cpu, mergeInst(5, 5, 5));

    EXPECT_DOUBLE_EQ(cpu->m_fpr[5], 8.0);
    EXPECT_DOUBLE_EQ(cpu->m_ps1[5], 8.0);
}
