// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_mfspr
// */

#include "TestFixture.hpp"

// SPR encoding: SPR = (inst.rb << 5) | inst.ra
// LR  = SPR  8: ra=8,  rb=0
// CTR = SPR  9: ra=9,  rb=0
// XER = SPR  1: ra=1,  rb=0

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — read LR into GPR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_ReadLR)
{
    cpu->m_lr = 0xDEADBEEF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 8; // SPR low 5: LR=8
    inst.rb = 0; // SPR high 5

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — read CTR into GPR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_ReadCTR)
{
    cpu->m_ctr = 0x12345678;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 9; // SPR low 5: CTR=9
    inst.rb = 0; // SPR high 5

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x12345678u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — read XER into GPR
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_ReadXER)
{
    cpu->m_xer.raw = 0xE0000000; // SO=1, OV=1, CA=1

    EncodedInstruction inst(0);
    inst.rt = 6;
    inst.ra = 1; // SPR low 5: XER=1
    inst.rb = 0; // SPR high 5

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[6], 0xE0000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — LR=0 after reset reads as 0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_LR_ZeroAfterReset)
{
    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 8;
    inst.rb = 0;

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — RT=r0 is allowed
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_RT0_Allowed)
{
    cpu->m_lr = 0xCAFEBABE;

    EncodedInstruction inst(0);
    inst.rt = 0;
    inst.ra = 8;
    inst.rb = 0;

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[0], 0xCAFEBABEu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — does not modify SPR or other GPRs
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_DoesNotModifyOtherState)
{
    cpu->m_lr  = 0xAABBCCDD;
    cpu->m_ctr = 0x11223344;
    cpu->m_gpr[7] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 8; // read LR
    inst.rb = 0;

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_lr,      0xAABBCCDDu); // LR unchanged
    EXPECT_EQ(cpu->m_ctr,     0x11223344u); // CTR unchanged
    EXPECT_EQ(cpu->m_gpr[7],  0xFFFFFFFFu); // other GPR unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — read LR all-ones value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_LR_AllOnes)
{
    cpu->m_lr = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 8;
    inst.rb = 0;

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — read CTR all-ones value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_CTR_AllOnes)
{
    cpu->m_ctr = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 9;
    inst.rb = 0;

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR — unknown SPR hits default branch, GPR unchanged
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, MFSPR_UnknownSPR_GPRUnchanged)
{
    cpu->m_gpr[3] = 0xDEADC0DE;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 2; // spr = (ra<<5)|rb = 64 — not XER/LR/CTR
    inst.rb = 0;

    Core::Instruction::MFSPR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xDEADC0DEu);
}
