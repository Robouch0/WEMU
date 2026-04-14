/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_fmr
*/

#include "TestFixture.hpp"
#include <cmath>

// FMR: FRT = FRB  (copy FPR, preserving sign bit of all values including ±0)
// Fields: inst.rt=FRT(dest), inst.rb=FRB(source)

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — basic copy: positive value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_BasicCopy_Positive)
{
    cpu->m_fpr[2] = 3.14f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.14f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — copy negative value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_CopyNegative)
{
    cpu->m_fpr[2] = -2.718f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -2.718f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — copy +0.0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_CopyPositiveZero)
{
    cpu->m_fpr[2] = 0.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
    EXPECT_FALSE(std::signbit(cpu->m_fpr[4]));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — copy -0.0 preserves sign
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_CopyNegativeZero_PreservesSign)
{
    cpu->m_fpr[2] = -0.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -0.0f);
    EXPECT_TRUE(std::signbit(cpu->m_fpr[4]));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — copy 1.0f
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_CopyOne)
{
    cpu->m_fpr[2] = 1.0f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — FRB is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_DoesNotModifyFRB)
{
    cpu->m_fpr[2] = 5.5f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 2;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[2], 5.5f); // FRB unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — FRT=FRB (in-place copy): value unchanged
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_InPlace_NoChange)
{
    cpu->m_fpr[4] = 7.77f;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.rb = 4;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 7.77f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  FMR — different source and destination registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, FMR_DifferentRegisters)
{
    cpu->m_fpr[10] = -100.0f;

    EncodedInstruction inst(0);
    inst.rt = 20;
    inst.rb = 10;

    Core::Instruction::FMR(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[20], -100.0f);
    EXPECT_FLOAT_EQ(cpu->m_fpr[10], -100.0f); // source unchanged
}
