// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lbzu
// */

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — basic load + RA updated to EA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_BasicLoad)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 4, 0xDE);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000DEu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — RA is updated to the exact effective address
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_RAUpdatedToEA)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 8, 0xAB);
    cpu->m_gpr[2] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 2;
    inst.si = 8;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[2], TEST_ADDR + 8);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — negative displacement: RA decremented
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_NegativeDisplacement)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xCD);
    cpu->m_gpr[1] = TEST_ADDR + 16;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000CDu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR); // RA = TEST_ADDR + 16 - 16
}


//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — load zero value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_LoadZero)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 4, 0x00);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}


//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — load 0xFF (zero-extended, not sign-extended)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_Load0xFF_ZeroExtended)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 4, 0xFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000FFu); // NOT 0xFFFFFFFF
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — load 0x80 (zero-extended, not sign-extended)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_Load0x80_ZeroExtended)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 4, 0x80);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000080u); // NOT 0xFFFFFF80
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — chained: use updated RA for second load
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_ChainedLoads)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 4, 0xAA);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 8, 0xBB);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst1(0);
    inst1.rt = 4;
    inst1.ra = 1;
    inst1.si = 4;
    Core::Instruction::LBZU(*cpu, inst1);

    EncodedInstruction inst2(0);
    inst2.rt = 5;
    inst2.ra = 1;
    inst2.si = 4;
    Core::Instruction::LBZU(*cpu, inst2);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000AAu);
    EXPECT_EQ(cpu->m_gpr[5], 0x000000BBu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 8);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — RT and RA are different registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_DifferentRTAndRA)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 12, 0x55);
    cpu->m_gpr[3] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 7;
    inst.ra = 3;
    inst.si = 12;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[7], 0x00000055u);
    EXPECT_EQ(cpu->m_gpr[3], TEST_ADDR + 12);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — displacement of 0 (RA = original RA, no net change)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_ZeroDisplacement_RAUnchanged)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xCC);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000CCu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — RA updated before RT is written (RT alias must use original RA)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_RA_EA_IsComputedBeforeUpdate)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 4, 0x11);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000011u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZU — large negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZU_LargeNegativeDisplacement)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xFE);
    cpu->m_gpr[1] = TEST_ADDR + 0x100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-0x100));

    Core::Instruction::LBZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000FEu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}
