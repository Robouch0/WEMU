/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_lhax
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — basic sign extension: positive halfword
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_BasicPositive)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x0042);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000042u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — 0x8000 sign-extended to 0xFFFF8000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_SignExtend_0x8000)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x8000);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — 0xFFFF sign-extended to 0xFFFFFFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_SignExtend_0xFFFF)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0xFFFF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_gprSigned[4], -1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — 0x7FFF stays as 0x00007FFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_NoSignExtend_0x7FFF)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x7FFF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00007FFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — zero halfword
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_LoadZero)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0000);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — RA=0: uses 0 as base, RB provides address
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_RA0_RBProvidesAddress)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x8080);
    cpu->m_gpr[0] = 0xDEAD0000; // ignored
    cpu->m_gpr[2] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8080u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — RB=0: EA = RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_RB0_EAEqualsRA)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x1234);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00001234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x5678);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — 0x8001 sign-extended to 0xFFFF8001
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_SignExtend_0x8001)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x8001);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — 0x0001 stays as 0x00000001
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_SmallPositive)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 2, 0x0001);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 2;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LHAX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LHAX — different RT, RA, RB registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LHAX_DifferentRegisters)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR + 8, 0xFFFE);
    cpu->m_gpr[5] = TEST_ADDR;
    cpu->m_gpr[6] = 8;

    EncodedInstruction inst(0);
    inst.rt = 10;
    inst.ra = 5;
    inst.rb = 6;

    Core::Instruction::LHAX(*cpu, inst);

    // 0xFFFE sign-extended → 0xFFFFFFFE
    EXPECT_EQ(cpu->m_gpr[10], 0xFFFFFFFEu);
}
