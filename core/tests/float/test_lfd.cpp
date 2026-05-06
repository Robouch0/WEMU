// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lfd
// */

#include <cstring>

#include "TestFixture.hpp"

static constexpr uint32_t TEST_ADDR = 0x02000200;

// LFD: FRT = double loaded from EA (64-bit IEEE 754).
// EA = (RA|0) + EXTS(D). RA=0 means base is 0 (not r0).
// Note: m_fpr is float[], so LFD stores the value at float precision.
// Fields: inst.rt=FRT, inst.ra=RA, inst.si=D

static void writeDouble(Core::Interpreter *cpu, uint32_t addr, float val)
{
    // Write a float value as 8 bytes (double representation) for LFD
    // For testing purposes, we write a value that is exactly representable
    // in both float and double to keep assertions simple.
    uint32_t hi, lo;
    // Convert float → double bit pattern and write high/low 32-bit words
    double d = static_cast<double>(val);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    hi = static_cast<uint32_t>(bits >> 32);
    lo = static_cast<uint32_t>(bits & 0xFFFFFFFF);
    cpu->m_memory.write<uint32_t>(addr, hi);
    cpu->m_memory.write<uint32_t>(addr + 4, lo);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — basic load of 1.0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_BasicLoad_One)
{
    writeDouble(cpu, TEST_ADDR, 1.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — load -1.0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_LoadNegativeOne)
{
    writeDouble(cpu, TEST_ADDR, -1.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -1.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — load 0.0
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_LoadZero)
{
    writeDouble(cpu, TEST_ADDR, 0.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — positive displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_PositiveDisplacement)
{
    writeDouble(cpu, TEST_ADDR + 8, 2.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 8;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 2.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — RA=0 uses 0 as base → exception
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_RA0_Uses0NotR0)
{
    cpu->m_gpr[0] = TEST_ADDR; // r0 must be ignored

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 0;
    inst.si = 0;

    EXPECT_THROW(Core::Instruction::LFD(*cpu, inst), Core::MemoryException);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — does not update RA
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_DoesNotUpdateRA)
{
    writeDouble(cpu, TEST_ADDR, 1.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — load -2.5
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_LoadNegative)
{
    writeDouble(cpu, TEST_ADDR, -2.5f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], -2.5f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — FRT=f0 is allowed
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_FRT0_Allowed)
{
    writeDouble(cpu, TEST_ADDR, 2.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 0;
    inst.ra = 1;
    inst.si = 0;

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[0], 2.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — maximum positive displacement (0x7FF8, 8-byte aligned): no throw
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_MaxPositiveSI)
{
    writeDouble(cpu, TEST_ADDR + 0x7FF8, 1.0f);
    cpu->m_gpr[1] = TEST_ADDR;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 1;
    inst.si = 0x7FF8; // largest 8-byte-aligned positive displacement

    EXPECT_NO_THROW(Core::Instruction::LFD(*cpu, inst));
    EXPECT_FLOAT_EQ(cpu->m_fpr[5], 1.0f);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  LFD — negative displacement
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, LFD_NegativeDisplacement)
{
    writeDouble(cpu, TEST_ADDR, 3.0f);
    cpu->m_gpr[1] = TEST_ADDR + 16;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 1;
    inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));

    Core::Instruction::LFD(*cpu, inst);

    EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.0f);
}
