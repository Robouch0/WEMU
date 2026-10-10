/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_lswi
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LSWI — full word (NB=4) packs bytes big-endian into RT
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LSWI_FullWord)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 0, 0xDE);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 1, 0xAD);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 2, 0xBE);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 3, 0xEF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.rb = 4; // NB

    Core::Instruction::LSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LSWI — partial word (NB=3) zero-pads the remaining low byte
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LSWI_PartialWordZeroPadded)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 0, 0x11);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 1, 0x22);
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 2, 0x33);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[5] = 0xFFFFFFFF; // must be fully overwritten

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.rb = 3;

    Core::Instruction::LSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x11223300u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LSWI — NB=0 means 32 bytes into 8 consecutive registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LSWI_NBZeroLoads32Bytes)
{
    for (uint32_t i = 0; i < 32; i++)
        cpu->m_memory.write<uint8_t>(TEST_ADDR + i, static_cast<uint8_t>(i));
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 10;
    inst.ra = 1;
    inst.rb = 0; // NB=0 -> 32 bytes

    Core::Instruction::LSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[10], 0x00010203u);
    EXPECT_EQ(cpu->m_gpr[11], 0x04050607u);
    EXPECT_EQ(cpu->m_gpr[17], 0x1C1D1E1Fu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LSWI — register index wraps from r31 to r0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LSWI_WrapsR31ToR0)
{
    for (uint32_t i = 0; i < 8; i++)
        cpu->m_memory.write<uint8_t>(TEST_ADDR + i, static_cast<uint8_t>(0xA0 + i));
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 31;
    inst.ra = 1;
    inst.rb = 8;

    Core::Instruction::LSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[31], 0xA0A1A2A3u);
    EXPECT_EQ(cpu->m_gpr[0], 0xA4A5A6A7u);
}
