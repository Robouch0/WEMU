// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stw
// */

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — basic store
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_BasicStore)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xDEADBEEF;

    EncodedInstruction inst(0);
    inst.rt = 4; // RS uses same bits as RT
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — RA=0 uses 0 as base, not r0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_RA0_Uses0NotR0)
{
    cpu->m_gpr[0] = TEST_ADDR; // r0 has an address but must NOT be used as base
    cpu->m_gpr[4] = 0x12345678;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0; // EA = 0 + EXTS(D) = 0 (unmapped)
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    // Nothing should have been written to TEST_ADDR
    EXPECT_NE(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x12345678u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_PositiveDisplacement)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xCAFEBABE;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 8;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8), 0xCAFEBABEu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_NegativeDisplacement)
{
    cpu->m_gpr[1] = TEST_ADDR + 16;
    cpu->m_gpr[4] = 0xABCD1234;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xABCD1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — store 0x00000000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_StoreZero)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xFFFFFFFF); // pre-fill
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — store 0xFFFFFFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_StoreAllOnes)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xFFFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — store 0x80000000
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_StoreHighBit)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x80000000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x80000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — store 0x7FFFFFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_StoreMaxPositive)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x7FFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x7FFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_DoesNotUpdateRA)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0x11223344;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — successive stores to adjacent words
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_SuccessiveAdjacentStores)
{
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[4] = 0xAAAAAAAA;
    cpu->m_gpr[5] = 0xBBBBBBBB;

    EncodedInstruction inst1(0);
    inst1.rt = 4; inst1.ra = 1; inst1.si = 0;
    Core::Instruction::STW(*cpu, inst1);

    EncodedInstruction inst2(0);
    inst2.rt = 5; inst2.ra = 1; inst2.si = 4;
    Core::Instruction::STW(*cpu, inst2);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),     0xAAAAAAAAu);
    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0xBBBBBBBBu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  STW — store r0 as RS (allowed)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, STW_RS0_Allowed)
{
    cpu->m_gpr[0] = 0xFEEDFACE;
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 0; // RS = r0
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::STW(*cpu, inst);

    EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xFEEDFACEu);
}
