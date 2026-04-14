/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_fctiwz
*/

#include "TestFixture.hpp"
#include <cstring>

// FCTIWZ: FRT = (int32_t)FRB  — convert FPR to integer, ALWAYS truncating toward zero
// Ignores FPSCR rounding mode. Result is stored as 32-bit bit pattern in FRT.
// Fields: inst.rt=FRT(dest), inst.rb=FRB(source)

static int32_t fprToIntZ(float fprVal)
{
    int32_t result;
    std::memcpy(&result, &fprVal, sizeof(result));
    return result;
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — convert 1.0 → 1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_One)
{
    cpu->m_fpr[2] = 1.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), 1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — convert -1.0 → -1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_NegativeOne)
{
    cpu->m_fpr[2] = -1.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), -1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — convert 0.0 → 0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_Zero)
{
    cpu->m_fpr[2] = 0.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), 0);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — truncation: 1.9 → 1 (not 2)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_TruncatesDown)
{
    cpu->m_fpr[2] = 1.9f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), 1); // truncate toward zero, not round
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — truncation: -1.9 → -1 (toward zero, not -2)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_TruncatesNegativeTowardZero)
{
    cpu->m_fpr[2] = -1.9f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), -1); // not -2
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — truncation: 1.5 → 1 (not 2 like round-to-nearest)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_Truncates_1_5)
{
    cpu->m_fpr[2] = 1.5f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), 1); // truncated, not rounded to 2
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — convert 100.7 → 100
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_PositiveFractional)
{
    cpu->m_fpr[2] = 100.7f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_EQ(fprToIntZ(cpu->m_fpr[4]), 100);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FCTIWZ — FRB is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FCTIWZ_DoesNotModifyFRB)
{
    cpu->m_fpr[2] = 42.9f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FCTIWZ(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[2], 42.9f);
}
