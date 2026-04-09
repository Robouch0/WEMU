// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lwz
// */
//
#include "TestFixture.hpp"
//
static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — basic load
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_BasicLoad)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xDEADBEEF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — RA=0: uses 0 as base, not r0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_RA0_Uses0NotR0)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x12345678);
    cpu->m_gpr[0] = TEST_ADDR; // r0 has a valid address but must NOT be used as base

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0; // RA=0 → EA = 0 + EXTS(D)
    inst.si = 0;

    // EA = 0, which is unmapped → DSI exception
    EXPECT_THROW(Core::Instruction::LWZ(*cpu, inst), Core::MemoryException);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_PositiveDisplacement)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8, 0xCAFEBABE);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.si = 8;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xCAFEBABEu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_NegativeDisplacement)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xABCD1234);
    cpu->m_gpr[1] = TEST_ADDR + 16;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xABCD1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — load zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_LoadZero)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x00000000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — load 0xFFFFFFFF (no sign extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_LoadAllOnes_NoSignExt)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xFFFFFFFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xFFFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — load 0x80000000 (high bit set, no sign extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_HighBitSet_NoSignExt)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x80000000);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0x80000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — load 0x7FFFFFFF
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_LoadMaxPositive)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x7FFFFFFF);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 3;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0x7FFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — RT=0 is allowed
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_RT0_Allowed)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x11223344);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 0;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[0], 0x11223344u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — successive loads to different registers
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_SuccessiveLoads)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR,     0xAABBCCDD);
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x11223344);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst1(0);
    inst1.rt = 3; inst1.ra = 1; inst1.si = 0;
    Core::Instruction::LWZ(*cpu, inst1);

    EncodedInstruction inst2(0);
    inst2.rt = 4; inst2.ra = 1; inst2.si = 4;
    Core::Instruction::LWZ(*cpu, inst2);

    EXPECT_EQ(cpu->m_gpr[3], 0xAABBCCDDu);
    EXPECT_EQ(cpu->m_gpr[4], 0x11223344u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — RT does not alias RA (RA is not modified)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xFEEDFACE);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LWZ(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFEEDFACEu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR); // RA unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZ — maximum positive si displacement (0x7FFF = +32767)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZ_MaxPositiveSI)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 0x7FFC, 0xBEEFCAFE); // 0x7FFC aligned on 4
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.si = 0x7FFC; // same offset

    // EA = TEST_ADDR + 0x7FFC, mapped → read succeeds
    EXPECT_NO_THROW(Core::Instruction::LWZ(*cpu, inst));
    EXPECT_EQ(cpu->m_gpr[5], 0xBEEFCAFEu);
}
