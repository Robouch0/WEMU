// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_sth
// */

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — basic store: low 16 bits of RS
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_BasicStore)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x0000ABCD;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0xABCDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — high bits of RS are ignored
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_HighBitsIgnored)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xFFFF1234; // high 16 bits must be ignored

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0x1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — store 0xFFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_StoreAllOnes)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x0000FFFF;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0xFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — store 0x0000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_StoreZero)
{
    cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xFFFF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — store 0x8000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_Store0x8000)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00008000;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0x8000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — RA=0 uses 0 as base
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_RA0_Uses0AsBase)
{
    cpu->m_gpr[0] = TEST_ADDR; // r0 ignored
    cpu->m_gpr[4] = 0x0000BEEF;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 0;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    // EA = 0 → unmapped, TEST_ADDR untouched
    EXPECT_NE(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0xBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_PositiveDisplacement)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x0000CAFE;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 6;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR + 6), 0xCAFEu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_NegativeDisplacement)
{
    cpu->m_gpr[1] = TEST_ADDR + 4;
    cpu->m_gpr[4] = 0x00001234;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-4));

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR), 0x1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_DoesNotUpdateRA)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00005A5A;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STH — successive stores to adjacent halfwords
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STH_SuccessiveAdjacentStores)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x0000AAAA;
    cpu->m_gpr[5] = 0x0000BBBB;

    EncodedInstruction inst1(0);
    inst1.rs = 4; inst1.ra = 1; inst1.si = 0;
    Core::Instruction::STH(*cpu, inst1);

    EncodedInstruction inst2(0);
    inst2.rs = 5; inst2.ra = 1; inst2.si = 2;
    Core::Instruction::STH(*cpu, inst2);

    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR),     0xAAAAu);
    EXPECT_EQ(cpu->m_memory.read<uint16_t>(TEST_ADDR + 2), 0xBBBBu);
}
