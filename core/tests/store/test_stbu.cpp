// /*
// ** EPITECH PROJECT, 2026
// ** core
// ** File description:
// ** test_stbu
// */

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — basic store: low byte of RS
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_BasicStore)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000042;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x42u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — high bits of RS are ignored
// ─────────────────────────────────────────────────────────────────────────────
//
//

TEST_F(InstructionTest, STBU_HighBitsIgnored)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xFFFFFF42; // high 24 bits must be ignored

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x42u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — store 0xFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_Store0xFF)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x000000FF;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0xFFu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — store 0x00
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_StoreZero)
{
    cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xFF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — store 0x80
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_Store0x80)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000080;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x80u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — store 0x7F
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_Store0x7F)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x0000007F;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x7Fu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}


//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_PositiveDisplacement)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x000000CD;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 3;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 3), 0xCDu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR+3);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_NegativeDisplacement)
{
    cpu->m_gpr[1] = TEST_ADDR + 5;
    cpu->m_gpr[4] = 0x000000EF;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-5));

    Core::Instruction::STBU(*cpu, inst);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0xEFu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STBU_DoesNotUpdateRA)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000001;

    EncodedInstruction inst(0);
    inst.rs = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STBU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);

}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STBU — successive stores to adjacent bytes
// ─────────────────────────────────────────────────────────────────────────────
//


TEST_F(InstructionTest, STBU_SuccessiveAdjacentStores)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000011;
    cpu->m_gpr[5] = 0x00000022;

    EncodedInstruction inst1(0);
    inst1.rs = 4;
    inst1.ra = 1;
    inst1.si = 0;
    Core::Instruction::STBU(*cpu, inst1);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);



    EncodedInstruction inst2(0);
    inst2.rs = 5;
    inst2.ra = 1;
    inst2.si = 1;
    Core::Instruction::STBU(*cpu, inst2);

    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x11u);
    EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 1), 0x22u);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR+1);

}
