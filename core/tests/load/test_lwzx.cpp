// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lwzx
// */
//
#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — basic: EA = RA + RB
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_BasicLoad)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 8, 0xDEADBEEF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 8;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xDEADBEEFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — RA=0: uses 0 as base, not r0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_RA0_Uses0NotR0)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x12345678);
    cpu->m_gpr[0] = TEST_ADDR; // r0 has valid address but must NOT be used
    cpu->m_gpr[2] = 0;         // RB = 0 → EA = 0 + 0 = 0 (unmapped)

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.rb = 2;

    EXPECT_THROW(Core::Instruction::LWZX(*cpu, inst), Core::MemoryException);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — RA=0: RB provides the full address
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_RA0_RBProvidesAddress)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xCAFEBABE);
    cpu->m_gpr[0] = 0xDEAD0000; // r0 must be ignored
    cpu->m_gpr[2] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xCAFEBABEu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — RB=0: EA = RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_RB0_EAEqualsRA)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xABCD1234);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xABCD1234u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — load 0xFFFFFFFF (no sign extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_LoadAllOnes)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xFFFFFFFF);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — load 0x80000000 (high bit set, no sign extension)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_HighBitSet)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0x80000000);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — load zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_LoadZero)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x00000000);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — RA and RB both non-zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_BothRAAndRBNonZero)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 12, 0x55443322);
    cpu->m_gpr[1] = TEST_ADDR + 4;
    cpu->m_gpr[2] = 8;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x55443322u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_DoesNotUpdateRA)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xFEEDC0DE);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 4;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFEEDC0DEu);
    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR); // RA unchanged
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — large RB offset
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_LargeRBOffset)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR + 0x100, 0xBEEFCAFE);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0x100;

    EncodedInstruction inst(0);
    inst.rt = 6;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[6], 0xBEEFCAFEu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LWZX — RT=0 is allowed
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LWZX_RT0Allowed)
{
    cpu->m_memory.write<uint32_t>(TEST_ADDR, 0x99887766);
    cpu->m_gpr[1] = TEST_ADDR;
    cpu->m_gpr[2] = 0;

    EncodedInstruction inst(0);
    inst.rt = 0;
    inst.ra = 1;
    inst.rb = 2;

    Core::Instruction::LWZX(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[0], 0x99887766u);
}
