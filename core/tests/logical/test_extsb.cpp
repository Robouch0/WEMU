// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_extsb
// */

#include "TestFixture.hpp"

// EXTSB: rA = EXTS(rS[24:31])  — sign-extend the low 8 bits of rS to 32 bits
// Fields: inst.rt=RS(source), inst.ra=RA(dest)

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — positive small value: 0x01 → 0x00000001
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_PositiveSmall)
{
    cpu->m_gpr[3] = 0x00000001;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — max positive byte: 0x7F → 0x0000007F
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_MaxPositive)
{
    cpu->m_gpr[3] = 0x0000007F;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000007Fu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — min negative byte: 0x80 → 0xFFFFFF80
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_MinNegative)
{
    cpu->m_gpr[3] = 0x00000080;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFF80u);
    EXPECT_EQ(cpu->m_gprSigned[4], -128);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — all byte bits set: 0xFF → 0xFFFFFFFF (-1)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_AllOnes)
{
    cpu->m_gpr[3] = 0x000000FF;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_gprSigned[4], -1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — zero: stays zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_Zero)
{
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — high 24 bits of rS are ignored
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_HighBitsIgnored)
{
    cpu->m_gpr[3] = 0xABCDEF12; // low byte = 0x12 (positive)

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000012u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — high bits ignored even when low byte is negative
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_HighBitsIgnored_NegativeLowByte)
{
    cpu->m_gpr[3] = 0x00000081; // low byte = 0x81 → negative

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFF81u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — RC=1 updates CR0: negative result
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_RC_UpdatesCR0_Negative)
{
    cpu->m_gpr[3] = 0x000000FF; // sign-extends to -1

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rc = 1;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — RC=1 updates CR0: positive result
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_RC_UpdatesCR0_Positive)
{
    cpu->m_gpr[3] = 0x0000007F;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;
    inst.rc = 1;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000007Fu);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  EXTSB — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xABCDEFAB;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 4;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xABCDEFABu); // RS unchanged
}
