// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lhz
// */

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — basic zero extension
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_BasicLoad)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x1234);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00001234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — 0x8000 zero-extended to 0x00008000 (NOT sign-extended, unlike LHA)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_0x8000_NotSignExtended)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x8000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00008000u); // NOT 0xFFFF8000
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — 0xFFFF zero-extended to 0x0000FFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_AllOnes_ZeroExtended)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xFFFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000FFFFu); // NOT 0xFFFFFFFF
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — zero halfword
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_LoadZero)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — 0x7FFF stays as 0x00007FFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_0x7FFF_ZeroExtended)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x7FFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00007FFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — RA=0 uses 0 as base
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_RA0_Uses0AsBase)
{
    cpu->m_gpr[0] = TEST_ADDR; // r0 must be ignored

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u); // unmapped
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_PositiveDisplacement)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 6, 0xABCD);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 6;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000ABCDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_NegativeDisplacement)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xBEEF);
    cpu->m_gpr[1] = TEST_ADDR + 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-4));

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000BEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x5555);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — 0x0001 small positive value
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_SmallValue)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0001);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHZ — high bits of RT are cleared
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHZ_HighBitsOfRTCleared)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x00FF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xFFFFFFFF; // pre-set to verify high bits are cleared

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LHZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000FFu); // high 16 bits cleared
}
