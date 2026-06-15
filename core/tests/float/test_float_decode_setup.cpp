/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_float_decode_setup
*/

#include "TestFixture.hpp"

TEST_F(InstructionTest, FloatDecodeFields_RegisterAliases)
{
    EncodedInstruction inst(0);

    inst.frt = 4;
    inst.fra = 5;
    inst.frb = 6;
    inst.frc = 7;
    inst.rc = 1;

    EXPECT_EQ(inst.rt, 4u);
    EXPECT_EQ(inst.frt, 4u);
    EXPECT_EQ(inst.ra, 5u);
    EXPECT_EQ(inst.fra, 5u);
    EXPECT_EQ(inst.rb, 6u);
    EXPECT_EQ(inst.frb, 6u);
    EXPECT_EQ(inst.frc, 7u);
    EXPECT_EQ(inst.rc, 1u);
}

TEST_F(InstructionTest, FloatDecodeFields_CompareBF)
{
    EncodedInstruction inst(0);

    inst.bf = 3;
    inst.fra = 5;
    inst.frb = 6;

    EXPECT_EQ(inst.bf, 3u);
    EXPECT_EQ(inst.fra, 5u);
    EXPECT_EQ(inst.frb, 6u);
}

TEST_F(InstructionTest, FloatDecodeFields_XO5IgnoresFrcOperandBits)
{
    cpu->m_instructionMap[59] = {
            {InstructionID::E_FMR, {OPCD(59), XO5(21)}, Core::Instruction::FMR},
    };

    EncodedInstruction first(0);
    first.opcd = 59;
    first.xo5 = 21;
    first.frc = 0;

    EncodedInstruction second(0);
    second.opcd = 59;
    second.xo5 = 21;
    second.frc = 31;

    EXPECT_EQ(cpu->findInstructionID(first), InstructionID::E_FMR);
    EXPECT_EQ(cpu->findInstructionID(second), InstructionID::E_FMR);
}

TEST_F(InstructionTest, FloatDecodeFields_XO5MismatchDoesNotMatch)
{
    cpu->m_instructionMap[59] = {
            {InstructionID::E_FMR, {OPCD(59), XO5(21)}, Core::Instruction::FMR},
    };

    EncodedInstruction inst(0);
    inst.opcd = 59;
    inst.xo5 = 20;
    inst.frc = 7;

    EXPECT_THROW(cpu->findInstructionID(inst), Core::InterpreterException);
}
