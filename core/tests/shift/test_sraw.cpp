// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_sraw
// */
//
#include "TestFixture.hpp"

// SRAW: rA = rS >>signed rB[27:31]  (arithmetic right shift, sign-fill)
// Also sets XER[CA] if rS is negative and any 1-bits were shifted out.
// Fields: inst.rs=RS, inst.ra=RA(dest), inst.rb=RB(shift amount register)

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — basic: positive value behaves like logical shift
// ─────────────────────────────────────────────────────────────────────────────
//

// 0X00000100 | 00000000 = 0X00000100

TEST_F(InstructionTest, SRAW_ShiftPositive)
{
    cpu->m_gpr[3] = 0x00000010; // +16
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000008u);
    EXPECT_EQ(cpu->m_xer.ca, 0u); // no CA for positive
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — negative value: high bits sign-extended
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_ShiftNegative_SignExtend)
{
    cpu->m_gpr[3] = 0xFFFFFFFF; // -1
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu); // -1 >> 1 = -1 (sign fill)
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — negative with 1-bits shifted out: CA=1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_Negative_CA_Set)
{
    cpu->m_gpr[3] = 0x80000001; // negative, LSB=1
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xC0000000u); // arithmetic shift
    EXPECT_EQ(cpu->m_xer.ca, 1u); // 1-bit was shifted out of negative number
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — negative with all-zero low bits shifted out: CA=0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_Negative_CA_Clear)
{
    cpu->m_gpr[3] = 0xFFFFFF00; // negative, low 8 bits all 0
    cpu->m_gpr[5] = 4;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ca, 0u); // no 1-bits shifted out of negative number
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — shift by 0: value unchanged, CA=0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_ShiftBy0)
{
    cpu->m_gpr[3] = 0xDEADBEEF;
    cpu->m_gpr[5] = 0;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — shift negative by 31: fills with all 1s
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_ShiftBy31_Negative)
{
    cpu->m_gpr[3] = 0x80000000; // most-negative value
    cpu->m_gpr[5] = 31;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu); // all 1s (sign-fill)
    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — positive value never sets CA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_Positive_NeverSetsCA)
{
    cpu->m_gpr[3] = 0x7FFFFFFF; // max positive
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — RC=1 updates CR0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_RC_UpdatesCR0)
{
    cpu->m_gpr[3] = 0xFFFFFFFF; // -1
    cpu->m_gpr[5] = 1;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;
    inst.rc = 1;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_NE(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative, 0u); // lt=1
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive, 0u); // gt=0
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero, 0u); // eq=0
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xCAFEBABE;
    cpu->m_gpr[5] = 2;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xCAFEBABEu); // RS unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAW — CA is set
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAW_CA_Set)
{
    cpu->m_gpr[3] = 0xFFFFFFF1; // negative with 1 in last position
    cpu->m_gpr[5] = 3;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.rb = 5;

    Core::Instruction::SRAW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFEu);
    EXPECT_EQ(cpu->m_xer.ca, 1u);
}
