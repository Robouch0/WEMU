// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lbz
// */

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — basic load
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_BasicLoad)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x42);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000042u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — 0xFF zero-extended to 0x000000FF (not sign-extended)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_0xFF_NotSignExtended)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000FFu); // NOT 0xFFFFFFFF
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — 0x80 zero-extended to 0x00000080 (not sign-extended)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_0x80_NotSignExtended)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x80);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000080u); // NOT 0xFFFFFF80
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — zero byte
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_LoadZero)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x00);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — 0x7F zero-extended
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_0x7F_ZeroExtended)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x7F);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000007Fu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — RA=0 uses 0 as base
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_RA0_Uses0AsBase)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xAB);
    cpu->m_gpr[0] = TEST_ADDR; // r0 must be ignored

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.si = 0;
    inst.d = 0;

    // EA = 0 → unmapped → 0
    EXPECT_THROW(Core::Instruction::LBZ(*cpu, inst), Core::MemoryException);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_PositiveDisplacement)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 3, 0xCD);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 3;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000CDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_NegativeDisplacement)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xEF);
    cpu->m_gpr[1] = TEST_ADDR + 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-5));

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000EFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x01);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — RT=0 is allowed
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_RT0_Allowed)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x55);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 0;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[0], 0x00000055u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — load 0x01 (smallest non-zero)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_SmallestNonZero)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x01);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LBZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LBZ — successive loads from adjacent bytes
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LBZ_SuccessiveAdjacentLoads)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR,     0x11);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 1, 0x22);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst1(0);
    inst1.rt = 3; inst1.ra = 1; inst1.si = 0;
    Core::Instruction::LBZ(*cpu, inst1);

    EncodedInstruction inst2(0);
    inst2.rt = 4; inst2.ra = 1; inst2.si = 1;
    Core::Instruction::LBZ(*cpu, inst2);

    EXPECT_EQ(cpu->m_gpr[3], 0x00000011u);
    EXPECT_EQ(cpu->m_gpr[4], 0x00000022u);
}
