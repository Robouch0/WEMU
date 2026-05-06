/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_lwzu
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — basic: load word + RA updated to EA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_BasicLoadAndUpdate)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xDEADBEEF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4); // RA = EA
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — RA is updated to the exact effective address
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_RAUpdatedToEA)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8, 0x12345678);
    cpu->m_gpr[2] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 2;
    inst.si = 8;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[2], TEST_ADDR + 8);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — negative displacement: RA decremented
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_NegativeDisplacement)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xCAFEBABE);
    cpu->m_gpr[1] = TEST_ADDR + 16;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xCAFEBABEu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR); // RA = TEST_ADDR + 16 - 16
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — load zero value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_LoadZero)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x00000000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — load 0xFFFFFFFF (no sign extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_LoadAllOnes)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xFFFFFFFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — load 0x80000000 (high bit set, no sign extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_LoadHighBitSet)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x80000000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — chained: use updated RA for second load
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_ChainedLoads)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xAAAAAAAA);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8, 0xBBBBBBBB);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst1(0);
    inst1.rt = 4;
    inst1.ra = 1;
    inst1.si = 4;
    Core::Instruction::LWZU(*cpu, inst1);

    // After first: r1 = TEST_ADDR+4, r4 = 0xAAAAAAAA

    EncodedInstruction inst2(0);
    inst2.rt = 5;
    inst2.ra = 1;
    inst2.si = 4;
    Core::Instruction::LWZU(*cpu, inst2);

    EXPECT_EQ(cpu->m_gpr[4], 0xAAAAAAAAu);
    EXPECT_EQ(cpu->m_gpr[5], 0xBBBBBBBBu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 8);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — RT and RA are different registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_DifferentRTAndRA)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 12, 0x55667788);
    cpu->m_gpr[3] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 7;
    inst.ra = 3;
    inst.si = 12;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[7], 0x55667788u);
    EXPECT_EQ(cpu->m_gpr[3], TEST_ADDR + 12);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — displacement of 0 (RA = original RA, no net change)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_ZeroDisplacement_RAUnchanged)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x99AABBCC);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x99AABBCCu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR); // EA = TEST_ADDR + 0 = TEST_ADDR
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — RA updated before RT is written (RT alias must use original RA)
//         PPC ISA: EA computed from original RA before RT is updated
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_RA_EA_IsComputedBeforeUpdate)
{
    // Write 0x11111111 at TEST_ADDR + 4
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x11111111);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 4;

    Core::Instruction::LWZU(*cpu, inst);

    // EA = TEST_ADDR + 4 → load from there → RT = 0x11111111
    // RA = EA = TEST_ADDR + 4
    EXPECT_EQ(cpu->m_gpr[4], 0x11111111u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZU — large negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZU_LargeNegativeDisplacement)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xFACEFACE);
    cpu->m_gpr[1] = TEST_ADDR + 0x100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-0x100));

    Core::Instruction::LWZU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFACEFACEu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}
