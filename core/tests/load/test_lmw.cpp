/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_lmw
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — load single register (RT=31)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_SingleRegister)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xDEADBEEF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 31;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[31], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — load 2 registers (RT=30): r30, r31
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_TwoRegisters)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR,     0x11111111);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x22222222);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 30;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[30], 0x11111111u);
    EXPECT_EQ(cpu->m_gpr[31], 0x22222222u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — load 4 registers (RT=28): r28..r31
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_FourRegisters)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR,      0xAAAAAAAA);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4,  0xBBBBBBBB);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8,  0xCCCCCCCC);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 12, 0xDDDDDDDD);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 28;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[28], 0xAAAAAAAAu);
    EXPECT_EQ(cpu->m_gpr[29], 0xBBBBBBBBu);
    EXPECT_EQ(cpu->m_gpr[30], 0xCCCCCCCCu);
    EXPECT_EQ(cpu->m_gpr[31], 0xDDDDDDDDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — registers loaded in sequential order (RT, RT+1, ..., r31)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_SequentialOrder)
{
    for (int i = 0; i < 4; ++i)
        cpu->m_memory.write<uint32_t>(TEST_ADDR + i * 4, static_cast<uint32_t>(i + 1) * 0x10101010);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 28;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[28], 0x10101010u);
    EXPECT_EQ(cpu->m_gpr[29], 0x20202020u);
    EXPECT_EQ(cpu->m_gpr[30], 0x30303030u);
    EXPECT_EQ(cpu->m_gpr[31], 0x40404040u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_PositiveDisplacement)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8, 0xFEEDFACE);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 12, 0xC0FFEE00);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 30;
    inst.ra = 1;
    inst.si = 8;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[30], 0xFEEDFACEu);
    EXPECT_EQ(cpu->m_gpr[31], 0xC0FFEE00u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_NegativeDisplacement)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR,     0x12345678);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x9ABCDEF0);
    cpu->m_gpr[1] = TEST_ADDR + 8;

    EncodedInstruction inst(0);
    inst.rt = 30;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-8));

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[30], 0x12345678u);
    EXPECT_EQ(cpu->m_gpr[31], 0x9ABCDEF0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — RA=0 uses 0 as base
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_RA0_Uses0AsBase)
{
    cpu->m_gpr[0] = TEST_ADDR; // r0 ignored as base

    EncodedInstruction inst(0);
    inst.rt = 31;
    inst.ra = 0;
    inst.si = 0;

    // EA = 0, which is unmapped → DSI exception
    EXPECT_THROW(Core::Instruction::LMW(*cpu, inst), Core::MemoryException);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — load zeros
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_LoadZeros)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR,     0x00000000);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x00000000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 30;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[30], 0u);
    EXPECT_EQ(cpu->m_gpr[31], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — 3 registers (RT=29)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_ThreeRegisters)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR,     0x0A0B0C0D);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x1A1B1C1D);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8, 0x2A2B2C2D);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 29;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[29], 0x0A0B0C0Du);
    EXPECT_EQ(cpu->m_gpr[30], 0x1A1B1C1Du);
    EXPECT_EQ(cpu->m_gpr[31], 0x2A2B2C2Du);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LMW — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LMW_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x55555555);
    cpu->m_gpr[2] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 31;
    inst.ra = 2;
    inst.si = 0;

    Core::Instruction::LMW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[2], TEST_ADDR);
}
