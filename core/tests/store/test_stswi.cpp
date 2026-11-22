/*
** EPITECH PROJECT, 2026
** core
** File description:
** test_stswi
*/

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  STSWI — full word (NB=4) stores RS bytes big-endian
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STSWI_FullWord)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[5] = 0xDEADBEEF;

    EncodedInstruction inst(0);
    inst.rs = 5;
    inst.ra = 1;
    inst.rb = 4; // NB

    Core::Instruction::STSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 0), 0xDEu);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 1), 0xADu);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 2), 0xBEu);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 3), 0xEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STSWI — partial word (NB=3) stores only the 3 most significant bytes
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STSWI_PartialWord)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[5] = 0x11223344;
    cpu->m_memory.write<uint8_t>(TEST_ADDR + 3, 0x99); // must stay untouched

    EncodedInstruction inst(0);
    inst.rs = 5;
    inst.ra = 1;
    inst.rb = 3;

    Core::Instruction::STSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 0), 0x11u);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 1), 0x22u);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 2), 0x33u);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 3), 0x99u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STSWI — NB=0 means 32 bytes from 8 consecutive registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STSWI_NBZeroStores32Bytes)
{
    cpu->m_gpr[1] = TEST_ADDR;
    for (uint32_t r = 0; r < 8; r++)
        cpu->m_gpr[10 + r] = 0x01010101u * (r + 1);

    EncodedInstruction inst(0);
    inst.rs = 10;
    inst.ra = 1;
    inst.rb = 0; // NB=0 -> 32 bytes

    Core::Instruction::STSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 0), 0x01010101u);
    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 28), 0x08080808u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STSWI — register index wraps from r31 to r0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STSWI_WrapsR31ToR0)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[31] = 0xA0A1A2A3;
    cpu->m_gpr[0] = 0xA4A5A6A7;

    EncodedInstruction inst(0);
    inst.rs = 31;
    inst.ra = 1;
    inst.rb = 8;

    Core::Instruction::STSWI(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 0), 0xA0A1A2A3u);
    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0xA4A5A6A7u);
}
