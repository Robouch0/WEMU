#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=100, RA=42, CA=1
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_SimplePositive)
{
    cpu->m_gpr[3] = 42;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 100;

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 58u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=42, RA=42, zero result, CA=1
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_ZeroResult)
{
    cpu->m_gpr[3] = 42;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 42;

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=10, RA=100, negative result, CA=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_NegativeResult)
{
    cpu->m_gpr[3] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 10;

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -90);
    EXPECT_EQ(cpu->m_xer.ca, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=50, RA=0, CA=1
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_RAZero)
{
    cpu->m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 50;

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 50u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=-1 (sign-extended), RA=10, CA=1
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_NegativeImmediate)
{
    cpu->m_gpr[3] = 10;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = static_cast<uint16_t>(-1);

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -11);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=0, RA=0xFFFFFFFF, CA=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_MaxRA_ZeroSI)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0;

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
}

//
// ───────────────────────────────────────────────────────────────
//  SUBFIC: SI=0, RA=1, CA=0
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SUBFIC_SmallRA_ZeroSI)
{
    cpu->m_gpr[3] = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0;

    Core::Instruction::SUBFIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -1);
    EXPECT_EQ(cpu->m_xer.ca, 0);
}
