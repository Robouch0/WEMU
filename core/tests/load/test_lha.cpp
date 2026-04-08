/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_lha
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — basic load, positive halfword (sign ext does not change value)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_BasicPositive)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0042);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000042u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — 0x8000 sign-extended to 0xFFFF8000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_SignExtend_0x8000)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x8000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — 0xFFFF sign-extended to 0xFFFFFFFF (-1)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_SignExtend_0xFFFF)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xFFFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_gprSigned[4], -1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — 0x7FFF stays as 0x00007FFF (high bit clear, no extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_NoSignExtend_0x7FFF)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x7FFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00007FFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — zero halfword
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_LoadZero)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — 0x8001 sign-extended to 0xFFFF8001
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_SignExtend_0x8001)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x8001);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_PositiveDisplacement)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 6, 0x8000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 6;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_NegativeDisplacement)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xABCD);
    cpu->m_gpr[1] = TEST_ADDR + 8;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-8));

    Core::Instruction::LHA(*cpu, inst);

    // 0xABCD has high bit set → sign-extended to 0xFFFFABCD
    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFABCDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — RA=0 uses 0 as base
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_RA0_Uses0AsBase)
{
    cpu->m_gpr[0] = TEST_ADDR; // r0 must be ignored

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.si = 0;

    // EA = 0, which is unmapped → DSI exception
    EXPECT_THROW(Core::Instruction::LHA(*cpu, inst), Core::MemoryException);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x1234);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHA — 0x0001 stays as 0x00000001
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHA_SmallPositiveValue)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0001);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHA(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}
