// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_fneg
// */

#include <cmath>

#include "TestFixture.hpp"

// FNEG: FRT = -FRB  (flip sign bit, no rounding)
// Fields: inst.rt=FRT(dest), inst.rb=FRB(source)

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — negate positive: result is negative
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_PositiveBecomesNegative)
{
    cpu->m_fpr[2] = 3.14f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FNEG(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -3.14f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — negate negative: result is positive
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_NegativeBecomesPositive)
{
    cpu->m_fpr[2] = -2.718f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FNEG(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 2.718f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — +0.0 becomes -0.0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_PositiveZero_BecomesNegativeZero)
{
    cpu->m_fpr[2] = 0.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FNEG(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -0.0f);
    EXPECT_TRUE(std::signbit(cpu->m_fpr[4]));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — -0.0 becomes +0.0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_NegativeZero_BecomesPositiveZero)
{
    cpu->m_fpr[2] = -0.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FNEG(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
    EXPECT_FALSE(std::signbit(cpu->m_fpr[4]));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — double negation restores original value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_DoubleNegation)
{
    cpu->m_fpr[2] = 5.5f;

    EncodedInstruction inst1(0);
    inst1.rt = 4;
    inst1.rb = 2;
    Core::Instruction::FNEG(*cpu, inst1);

    EncodedInstruction inst2(0);
    inst2.rt = 5;
    inst2.rb = 4;
    Core::Instruction::FNEG(*cpu, inst2);

    EXPECT_FLOAT_EQ(cpu->m_fpr[5], 5.5f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — FRB is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_DoesNotModifyFRB)
{
    cpu->m_fpr[2] = 1.23f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FNEG(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[2], 1.23f); // FRB unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — in-place: FRT=FRB sign is flipped
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_InPlace)
{
    cpu->m_fpr[4] = 9.9f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 4;

    Core::Instruction::FNEG(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -9.9f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FNEG — RC=1 updates CR1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FNEG_RC_UpdatesCR1)
{
    cpu->m_fpr[2] = 1.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;
    inst.rc = 1;

    EXPECT_NO_THROW(Core::Instruction::FNEG(*cpu, inst));
    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -1.0f);
}
