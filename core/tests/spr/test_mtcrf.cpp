/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_mtcrf
*/

#include "TestFixture.hpp"

// MTCRF encoding:
//   inst.rt  = RS (source GPR, uses same bits as RT)
//   FXM field is an 8-bit mask at C++ raw bits [12:19]:
//     inst.raw |= (fxm << 12)
//   FXM bit 7 (MSB) = CR0, bit 0 (LSB) = CR7.

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — FXM=0xFF: all CR fields updated from RS
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_AllFields_FXM0xFF)
{
    cpu->m_gpr[4] = 0xABCDEF01;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.raw |= (0xFF << 12); // FXM = all fields

    Core::Instruction::MTCRF(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.raw, 0xABCDEF01u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — FXM=0x00: no fields updated, CR unchanged
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_NoFields_FXM0x00)
{
    cpu->m_cr.raw  = 0x12345678;
    cpu->m_gpr[4]  = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    // FXM = 0 → inst.raw stays 0 for FXM bits

    Core::Instruction::MTCRF(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.raw, 0x12345678u); // CR unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — FXM=0x80: only CR0 updated
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_OnlyCR0_FXM0x80)
{
    // CR starts with all fields set to 0
    cpu->m_cr.raw = 0x00000000;
    // RS = 0xF0000000 → CR0 nibble = 0xF (LT=GT=EQ=SO=1)
    cpu->m_gpr[4] = 0xF0000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.raw |= (0x80 << 12); // FXM: only bit 7 (CR0)

    Core::Instruction::MTCRF(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr0.lt, 1u);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1u);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1u);
    EXPECT_EQ(cpu->m_cr.cr0.so, 1u);
    // CR1-CR7 must stay 0
    EXPECT_EQ(cpu->m_cr.raw & 0x0FFFFFFFu, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — FXM=0x01: only CR7 updated
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_OnlyCR7_FXM0x01)
{
    cpu->m_cr.raw = 0x00000000;
    // RS = 0x0000000F → CR7 nibble = 0xF
    cpu->m_gpr[4] = 0x0000000F;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.raw |= (0x01 << 12); // FXM: only bit 0 (CR7)

    Core::Instruction::MTCRF(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.cr7.lt, 1u);
    EXPECT_EQ(cpu->m_cr.cr7.gt, 1u);
    EXPECT_EQ(cpu->m_cr.cr7.eq, 1u);
    EXPECT_EQ(cpu->m_cr.cr7.so, 1u);
    // CR0-CR6 must stay 0
    EXPECT_EQ(cpu->m_cr.raw & 0xFFFFFFF0u, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — unmasked fields are preserved
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_PreservesUnmaskedFields)
{
    // Set CR to a known value
    cpu->m_cr.raw = 0xABCDEF12;
    // RS writes a completely different pattern
    cpu->m_gpr[4] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.raw |= (0x80 << 12); // update only CR0

    Core::Instruction::MTCRF(*cpu, inst);

    // CR0 should now be 0 (from RS), but CR1–CR7 must keep their original bits
    EXPECT_EQ(cpu->m_cr.raw & 0x0FFFFFFFu, 0xABCDEF12u & 0x0FFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — source GPR is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_DoesNotModifySourceGPR)
{
    cpu->m_gpr[4] = 0x12345678;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.raw |= (0xFF << 12);

    Core::Instruction::MTCRF(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x12345678u); // RS unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTCRF — write zero to all fields clears CR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTCRF_WriteZero_ClearsCR)
{
    cpu->m_cr.raw = 0xFFFFFFFF;
    cpu->m_gpr[4] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.raw |= (0xFF << 12);

    Core::Instruction::MTCRF(*cpu, inst);

    EXPECT_EQ(cpu->m_cr.raw, 0u);
}
