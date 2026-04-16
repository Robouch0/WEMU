// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_srawi
// */
// 
#include "TestFixture.hpp"

// SRAWI: rA = rS >>signed SH  (arithmetic right shift, immediate shift amount)
// SH is a 5-bit immediate field at inst.sh (C++ bits 11-15).
// Also sets XER[CA] if rS is negative and any 1-bits are shifted out.
// Fields: inst.rs=RS, inst.ra=RA(dest), inst.sh=SH(immediate)

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — basic: shift positive value by 1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_BasicShift)
{
    cpu->m_gpr[3] = 0x00000010; // +16

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 1; // SH=1

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000008u);
    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — shift negative value: sign-extended
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_ShiftNegative_SignExtend)
{
    cpu->m_gpr[3] = 0xFFFFFFFF; // -1

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 1; // SH=1

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu); // -1 >>s 1 = -1
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — CA=1 when negative with 1-bits shifted out
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_CA_Set)
{
    cpu->m_gpr[3] = 0x80000003; // negative, bits 0 and 1 set

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 2; // SH=2 — shifts out bits 0 and 1

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ca, 1u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — CA=0 when negative but no 1-bits shifted out
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_CA_Clear_NegativeNoLowOnes)
{
    cpu->m_gpr[3] = 0xFFFFFFF0; // negative, low 4 bits all 0

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 4; // SH=4 — shifts out 4 zero bits

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — SH=0: value unchanged, CA=0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_SH0_NoChange)
{
    cpu->m_gpr[3] = 0xDEADBEEF;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 0; // SH=0

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — SH=31: shift fills with sign bit
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_SH31_Negative)
{
    cpu->m_gpr[3] = 0x80000000; // most-negative

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 31; // SH=31

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — SH=31: positive value becomes 0 or 1
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_SH31_Positive)
{
    cpu->m_gpr[3] = 0x7FFFFFFF; // max positive

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 31;

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u); // positive >> 31 = 0
    EXPECT_EQ(cpu->m_xer.ca, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — RC=1 updates CR0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_RC_UpdatesCR0)
{
    cpu->m_gpr[3] = 0xFFFFFFFE; // -2

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 1; // SH=1
    inst.rc = 1;

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu); // -1
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1u);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0u);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  SRAWI — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, SRAWI_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xCAFEBABE;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 4;

    Core::Instruction::SRAWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xCAFEBABEu);
}
