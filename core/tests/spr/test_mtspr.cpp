/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_mtspr
*/

#include "TestFixture.hpp"

// SPR encoding: SPR = (inst.rb << 5) | inst.ra
// LR  = SPR  8: ra=8,  rb=0
// CTR = SPR  9: ra=9,  rb=0
// XER = SPR  1: ra=1,  rb=0

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — write GPR into LR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_WriteLR)
{
    cpu->m_gpr[4] = 0xDEADBEEF;

    EncodedInstruction inst(0);
    inst.rt = 4; // RS uses same bits as RT
    inst.ra = 8; // SPR low 5: LR=8
    inst.rb = 0; // SPR high 5

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_lr, 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — write GPR into CTR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_WriteCTR)
{
    cpu->m_gpr[5] = 0x12345678;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 9; // SPR low 5: CTR=9
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_ctr, 0x12345678u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — write GPR into XER
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_WriteXER)
{
    cpu->m_gpr[6] = 0xE0000000; // SO=1, OV=1, CA=1

    EncodedInstruction inst(0);
    inst.rt = 6;
    inst.ra = 1; // SPR low 5: XER=1
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_xer.raw, 0xE0000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — write zero to LR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_WriteZeroToLR)
{
    cpu->m_lr  = 0xFFFFFFFF; // pre-set
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 8;
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_lr, 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — write all-ones to CTR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_WriteAllOnesToCTR)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 9;
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_ctr, 0xFFFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — source GPR is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_DoesNotModifySourceGPR)
{
    cpu->m_gpr[4] = 0xAABBCCDD;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 8; // write to LR
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xAABBCCDDu); // r4 unchanged
    EXPECT_EQ(cpu->m_lr,     0xAABBCCDDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — write to LR does not affect CTR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_WriteLR_DoesNotAffectCTR)
{
    cpu->m_ctr = 0x99887766;
    cpu->m_gpr[4] = 0x11223344;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 8; // LR
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_ctr, 0x99887766u); // CTR unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR — RS=r0 as source is allowed
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MTSPR_RS0_Allowed)
{
    cpu->m_gpr[0] = 0xFEEDFACE;

    EncodedInstruction inst(0);
    inst.rt = 0; // RS = r0
    inst.ra = 8;
    inst.rb = 0;

    Core::Instruction::MTSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_lr, 0xFEEDFACEu);
}
