/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_fcmpu
*/

#include <cmath>
#include <limits>

#include "TestFixture.hpp"

// FCMPU: unordered float compare — sets a CR field (crfd=BF) with FL/FG/FE/FU
//   FL (less than)    → CR.lt
//   FG (greater than) → CR.gt
//   FE (equal)        → CR.eq
//   FU (unordered)    → CR.so  (set when either operand is NaN)
//
// BF field (3 bits) selects CR0–CR7. Fields: inst.bf=BF, inst.fra=FRA,
// inst.frb=FRB.

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — FRA < FRB: FL=1, FG=0, FE=0, FU=0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_LessThan)
{
    cpu->m_fpr[2] = 1.0f; // FRA
    cpu->m_fpr[3] = 2.0f; // FRB

    EncodedInstruction inst(0);
    inst.bf = 0; // BF=0 → CR0
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1u); // less than
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0u); // not unordered
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — FRA > FRB: FL=0, FG=1, FE=0, FU=0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_GreaterThan)
{
    cpu->m_fpr[2] = 3.0f; // FRA
    cpu->m_fpr[3] = 1.0f; // FRB

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1u); // greater than
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — FRA == FRB: FL=0, FG=0, FE=1, FU=0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_Equal)
{
    cpu->m_fpr[2] = 2.5f;
    cpu->m_fpr[3] = 2.5f;

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1u); // equal
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — NaN operand: FU=1 (unordered), all others 0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_NaN_Unordered)
{
    cpu->m_fpr[2] = std::numeric_limits<float>::quiet_NaN();
    cpu->m_fpr[3] = 1.0f;

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1u); // unordered
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — both NaN: FU=1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_BothNaN_Unordered)
{
    cpu->m_fpr[2] = std::numeric_limits<float>::quiet_NaN();
    cpu->m_fpr[3] = std::numeric_limits<float>::quiet_NaN();

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 1u); // unordered
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — +0.0 == -0.0: equal
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_PositiveZero_EqualTo_NegativeZero)
{
    cpu->m_fpr[2] = 0.0f;
    cpu->m_fpr[3] = -0.0f;

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — BF=1: result written to CR1, not CR0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_BF1_WritesCR1)
{
    cpu->m_fpr[2] = 1.0f;
    cpu->m_fpr[3] = 2.0f;

    EncodedInstruction inst(0);
    inst.bf = 1; // BF=1 → CR1
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr1 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1u); // less than in CR1
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u); // CR0 untouched
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — negative vs positive: FRA < FRB
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_NegativeVsPositive)
{
    cpu->m_fpr[2] = -1.0f;
    cpu->m_fpr[3] = 1.0f;

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — FPRs are not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_DoesNotModifyFPRs)
{
    cpu->m_fpr[2] = 1.0f;
    cpu->m_fpr[3] = 2.0f;

    EncodedInstruction inst(0);
    inst.bf = 0;
    inst.fra = 2;
    inst.frb = 3;

    Core::Instruction::FCMPU(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[2], 1.0f);
    EXPECT_FLOAT_EQ(cpu->m_fpr[3], 2.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCMPU — dispatch entry is registered
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCMPU_DispatchEntry)
{
    EncodedInstruction inst(0);
    inst.opcd = 63;
    inst.xo10 = 0;
    inst.bf = 2;
    inst.fra = 4;
    inst.frb = 5;

    EXPECT_EQ(cpu->findInstructionID(inst), InstructionID::E_FCMPU);
}
