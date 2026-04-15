/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_sthx
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — basic store: low 16 bits of RS
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_BasicStore)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;
    cpu->m_gpr[4] = 0x0000ABCD;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR + 4), 0xABCDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — high bits of RS are ignored
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_HighBitsOfRSIgnored)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;
    cpu->m_gpr[4] = 0xFFFF1234; // high 16 bits must be ignored

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0x1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — store 0xFFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_StoreAllOnes)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;
    cpu->m_gpr[4] = 0x0000FFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0xFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — store 0x0000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_StoreZero)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xFFFF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;
    cpu->m_gpr[4] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — store 0x8000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_StoreHighHalfBit)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;
    cpu->m_gpr[4] = 0x00008000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0x8000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — store 0x7FFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_Store0x7FFF)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 2;
    cpu->m_gpr[4] = 0x00007FFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR + 2), 0x7FFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — RA=0: RB provides full address
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_RA0_RBProvidesAddress)
{
    cpu->m_gpr[0] = 0xDEAD0000; // ignored
    cpu->m_gpr[2] = TEST_ADDR;
    cpu->m_gpr[4] = 0x0000BEEF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0xBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — RB=0: EA = RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_RB0_EAEqualsRA)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;
    cpu->m_gpr[4] = 0x00001234;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0x1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_DoesNotUpdateRA)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;
    cpu->m_gpr[4] = 0x00005678;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STHX — different RS, RA, RB registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STHX_DifferentRegisters)
{
    cpu->m_gpr[5]  = TEST_ADDR;
    cpu->m_gpr[6]  = 6;
    cpu->m_gpr[10] = 0x0000CAFE;

    EncodedInstruction inst(0);
    inst.rt = 10;
    inst.ra = 5;
    inst.rb = 6;

    Core::Instruction::STHX(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR + 6), 0xCAFEu);
}
