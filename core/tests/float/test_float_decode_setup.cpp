/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_float_decode_setup
*/

#include <bit>

#include "TestFixture.hpp"

TEST_F(InstructionTest, QuantizedUpdateCopyLoopAdvancesSourceAndDestination)
{
    constexpr unsigned source = 0x28001000, destination = 0x28002000;
    for (unsigned i = 0; i < 32; ++i)
        cpu->m_memory.write<unsigned>(source + i * 4, std::bit_cast<unsigned>(float(i) + .25f));
    cpu->m_gpr[8] = source - 8;
    cpu->m_gpr[10] = destination - 8;
    cpu->m_gqr[0] = 0;
    cpu->executeInstruction(EncodedInstruction((31u << 26) | (8u << 11) | (470u << 1))); // dcbi 0,r8 before copying
    // Four pairs followed by update-form loads/stores: a common 32-byte copy loop.
    for (unsigned block = 0; block < 4; ++block)
        for (const unsigned raw: {0xE0080008u, 0xF00A0008u, 0xE0080010u, 0xF00A0010u, 0xE0080018u, 0xF00A0018u, 0xE4080020u, 0xF40A0020u})
            cpu->executeInstruction(EncodedInstruction(raw));
    EXPECT_EQ(cpu->m_gpr[8], source + 120);
    EXPECT_EQ(cpu->m_gpr[10], destination + 120);
    for (unsigned i = 0; i < 32; ++i)
        EXPECT_EQ(cpu->m_memory.read<unsigned>(destination + i * 4), cpu->m_memory.read<unsigned>(source + i * 4));
}

TEST_F(InstructionTest, QuantizedUpdateFormsHonorSignedDisplacementAndGqr)
{
    constexpr unsigned source = 0x28001000, destination = 0x28002000;
    cpu->m_memory.write<std::uint8_t>(source, std::uint8_t(-12));
    cpu->m_gpr[8] = source + 16;
    cpu->m_gpr[10] = destination + 8;
    cpu->m_gqr[2] = (6u << 16) | (2u << 24) | 7u | (1u << 8);
    cpu->executeInstruction(EncodedInstruction((57u << 26) | (8u << 16) | (1u << 15) | (2u << 12) | 0xFF0));
    EXPECT_EQ(cpu->m_gpr[8], source);
    EXPECT_DOUBLE_EQ(cpu->m_fpr[0], -3.0);
    EXPECT_DOUBLE_EQ(cpu->m_ps1[0], 1.0);
    cpu->executeInstruction(EncodedInstruction((61u << 26) | (10u << 16) | (2u << 12) | 0xFF8));
    EXPECT_EQ(cpu->m_gpr[10], destination);
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(destination), std::uint16_t(-6));
    EXPECT_EQ(cpu->m_memory.read<std::uint16_t>(destination + 2), 2u);
}

TEST_F(InstructionTest, QuantizedUpdateFaultPreservesTheBaseRegister)
{
    for (const unsigned opcode: {57u, 61u}) {
        cpu->m_gpr[8] = 0x2000;
        EXPECT_THROW(cpu->executeInstruction(EncodedInstruction((opcode << 26) | (8u << 16))), Core::MemoryException);
        EXPECT_EQ(cpu->m_gpr[8], 0x2000u);
    }
}

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
