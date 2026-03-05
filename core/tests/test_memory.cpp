/*
** EPITECH PROJECT, 2025
** core
** File description:
** Unit tests for memory load/store instructions
*/

#include <gtest/gtest.h>

#define private public
#include "cpu/interpreter/Interpreter.hpp"
#undef private

#include "cpu/types/EncodedInstruction.hpp"

// Base address used in all memory tests: ApplicationCode + 0x100
static constexpr std::uint32_t kTestAddr = 0x02000100u;

// Raw offset into the memory arena (m_memory vector) for kTestAddr
static constexpr std::size_t kOffset = 0x100;

static Core::Interpreter makeCPUWithMemory()
{
    Core::Binary binary;
    binary.m_memory = Core::Memory(0x800000); // 8 MB arena at ApplicationCode
    // Move binary to avoid copying the Memory object: copying invalidates
    // m_memAddress (a cached vector data pointer) and causes crashes.
    return Core::Interpreter(std::move(binary));
}

// Convenience: set gpr[ra] = kTestAddr, inst.ra = ra, inst.si = 0
static void setupBaseAddr(Core::Interpreter &cpu, EncodedInstruction &inst, int ra)
{
    cpu.m_gpr[ra] = kTestAddr;
    inst.ra = ra;
    inst.si = 0;
}

//
// ───────────────────────────────────────────────
//  LWZ — load word and zero
// ───────────────────────────────────────────────
//

TEST(MemoryTest, LWZ_Basic)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    // Write 0x12345678 in big-endian
    mem[0] = 0x12; mem[1] = 0x34; mem[2] = 0x56; mem[3] = 0x78;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::LWZ(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 0x12345678u);
}

TEST(MemoryTest, LWZ_ZeroExtend)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x00; mem[3] = 0x42;

    EncodedInstruction inst(0);
    inst.rt = 7;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::LWZ(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[7], 0x42u);
}

//
// ───────────────────────────────────────────────
//  LWZU — load word and zero with update
// ───────────────────────────────────────────────
//

TEST(MemoryTest, LWZU_UpdatesRA)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    mem[0] = 0x12; mem[1] = 0x34; mem[2] = 0x56; mem[3] = 0x78;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 4); // ra=4

    Core::Instruction::LWZU(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 0x12345678u);
    EXPECT_EQ(cpu.m_gpr[4], kTestAddr);   // ra updated to effective address
}

//
// ───────────────────────────────────────────────
//  LBZ — load byte and zero
// ───────────────────────────────────────────────
//

TEST(MemoryTest, LBZ_Basic)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);
    mem[0] = 0xFF;

    EncodedInstruction inst(0);
    inst.rt = 6;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::LBZ(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[6], 0xFFu);
}

TEST(MemoryTest, LBZ_ZeroExtend)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);
    mem[0] = 0x80;

    EncodedInstruction inst(0);
    inst.rt = 7;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::LBZ(cpu, inst);

    // Zero-extend: 0x80 should not sign-extend to 0xFFFFFF80
    EXPECT_EQ(cpu.m_gpr[7], 0x80u);
}

//
// ───────────────────────────────────────────────
//  LBZU — load byte and zero with update
// ───────────────────────────────────────────────
//

TEST(MemoryTest, LBZU_UpdatesRA)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);
    mem[0] = 0x42;

    EncodedInstruction inst(0);
    inst.rt = 8;
    setupBaseAddr(cpu, inst, 4);

    Core::Instruction::LBZU(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[8], 0x42u);
    EXPECT_EQ(cpu.m_gpr[4], kTestAddr);
}

//
// ───────────────────────────────────────────────
//  LHZ — load halfword and zero
// ───────────────────────────────────────────────
//

TEST(MemoryTest, LHZ_Basic)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    // Write 0x1234 in big-endian
    mem[0] = 0x12; mem[1] = 0x34;

    EncodedInstruction inst(0);
    inst.rt = 9;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::LHZ(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[9], 0x1234u);
}

//
// ───────────────────────────────────────────────
//  STW — store word
// ───────────────────────────────────────────────
//

TEST(MemoryTest, STW_Basic)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    cpu.m_gpr[5] = 0xDEADBEEFu;

    EncodedInstruction inst(0);
    inst.rt = 5; // source register (STW uses rt as source)
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::STW(cpu, inst);

    // Big-endian in memory
    EXPECT_EQ(mem[0], 0xDEu);
    EXPECT_EQ(mem[1], 0xADu);
    EXPECT_EQ(mem[2], 0xBEu);
    EXPECT_EQ(mem[3], 0xEFu);
}

TEST(MemoryTest, STW_LWZ_Roundtrip)
{
    auto cpu = makeCPUWithMemory();

    cpu.m_gpr[5] = 0x12345678u;

    EncodedInstruction stw(0);
    stw.rt = 5;
    setupBaseAddr(cpu, stw, 3);
    Core::Instruction::STW(cpu, stw);

    cpu.m_gpr[6] = 0;
    EncodedInstruction lwz(0);
    lwz.rt = 6;
    setupBaseAddr(cpu, lwz, 3);
    Core::Instruction::LWZ(cpu, lwz);

    EXPECT_EQ(cpu.m_gpr[6], 0x12345678u);
}

//
// ───────────────────────────────────────────────
//  STWU — store word with update
// ───────────────────────────────────────────────
//

TEST(MemoryTest, STWU_UpdatesRA)
{
    auto cpu = makeCPUWithMemory();

    cpu.m_gpr[5] = 0xCAFEBABEu;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 4); // ra=4

    Core::Instruction::STWU(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], kTestAddr); // ra updated to effective address
}

//
// ───────────────────────────────────────────────
//  STB — store byte
// ───────────────────────────────────────────────
//

TEST(MemoryTest, STB_Basic)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    cpu.m_gpr[5] = 0xFF42u;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::STB(cpu, inst);

    EXPECT_EQ(mem[0], 0x42u); // only low byte stored
}

TEST(MemoryTest, STB_OnlyLowByte)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    cpu.m_gpr[5] = 0x1234ABCDu;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::STB(cpu, inst);

    EXPECT_EQ(mem[0], 0xCDu);
}

//
// ───────────────────────────────────────────────
//  STBU — store byte with update
// ───────────────────────────────────────────────
//

TEST(MemoryTest, STBU_UpdatesRA)
{
    auto cpu = makeCPUWithMemory();

    cpu.m_gpr[5] = 0x42u;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 4);

    Core::Instruction::STBU(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], kTestAddr);
}

//
// ───────────────────────────────────────────────
//  STH — store halfword
// ───────────────────────────────────────────────
//

TEST(MemoryTest, STH_Basic)
{
    auto cpu = makeCPUWithMemory();
    auto *mem = reinterpret_cast<std::uint8_t *>(
        cpu.m_binary.m_memory.m_memory.data() + kOffset);

    cpu.m_gpr[5] = 0x12345678u;

    EncodedInstruction inst(0);
    inst.rt = 5;
    setupBaseAddr(cpu, inst, 3);

    Core::Instruction::STH(cpu, inst);

    // Stores low halfword (0x5678) in big-endian
    EXPECT_EQ(mem[0], 0x56u);
    EXPECT_EQ(mem[1], 0x78u);
}
