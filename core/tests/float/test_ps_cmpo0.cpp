/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_ps_cmpo0
*/

#include <limits>

#include "TestFixture.hpp"

//
// ─────────────────────────────────────────────────────────────────────────────
//  PS_CMPO0 — less than sets LT in the selected CR field and FPCC
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, PS_CMPO0_LessThan)
{
    cpu->m_fpr[1] = 1.0;
    cpu->m_fpr[2] = 2.0;

    EncodedInstruction inst(0);
    inst.fra = 1;
    inst.frb = 2;
    inst.bf = 0;

    Core::Instruction::PS_CMPO0(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr0, Core::ConditionRegisterFlag::Negative);
    EXPECT_EQ(cpu->m_fpscr.fpcc, Core::ConditionRegisterFlag::Negative);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  PS_CMPO0 — greater than sets GT
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, PS_CMPO0_GreaterThan)
{
    cpu->m_fpr[1] = 3.0;
    cpu->m_fpr[2] = 2.0;

    EncodedInstruction inst(0);
    inst.fra = 1;
    inst.frb = 2;
    inst.bf = 2;

    Core::Instruction::PS_CMPO0(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr2, Core::ConditionRegisterFlag::Positive);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  PS_CMPO0 — equal sets EQ
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, PS_CMPO0_Equal)
{
    cpu->m_fpr[1] = 2.5;
    cpu->m_fpr[2] = 2.5;

    EncodedInstruction inst(0);
    inst.fra = 1;
    inst.frb = 2;
    inst.bf = 0;

    Core::Instruction::PS_CMPO0(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr0, Core::ConditionRegisterFlag::Zero);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  PS_CMPO0 — NaN sets UN (SO flag) and raises VXVC (ordered compare)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, PS_CMPO0_NaNSetsUnorderedAndVXVC)
{
    cpu->m_fpr[1] = std::numeric_limits<double>::quiet_NaN();
    cpu->m_fpr[2] = 1.0;

    EncodedInstruction inst(0);
    inst.fra = 1;
    inst.frb = 2;
    inst.bf = 0;

    Core::Instruction::PS_CMPO0(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr0, Core::ConditionRegisterFlag::SummaryOverflow);
    EXPECT_TRUE(cpu->m_fpscr.vxvc);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  PS_CMPO0 — compares ps0 lane only (ps1 is ignored)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, PS_CMPO0_IgnoresPs1Lane)
{
    cpu->m_fpr[1] = 1.0;
    cpu->m_ps1[1] = 9.0;
    cpu->m_fpr[2] = 1.0;
    cpu->m_ps1[2] = -9.0;

    EncodedInstruction inst(0);
    inst.fra = 1;
    inst.frb = 2;
    inst.bf = 0;

    Core::Instruction::PS_CMPO0(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr0, Core::ConditionRegisterFlag::Zero);
}
