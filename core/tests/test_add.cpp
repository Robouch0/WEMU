
#include <gtest/gtest.h>

#define private public
#include "cpu/interpreter/Interpreter.hpp"
#undef private

#include "cpu/types/EncodedInstruction.hpp"

static Core::Interpreter makeCPU()
{
    const Core::Binary dummyBinary = {};

    return Core::Interpreter(dummyBinary);
}

//
// ───────────────────────────────────────────────────────────────
//  ADD TESTS
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADD_basic)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 32;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;

    Core::Instruction::ADD(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 42);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDE TESTS
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDE_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 20;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 30);
}

TEST(InstructionTest, ADDE_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 10;
    cpu.m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 21);
}

TEST(InstructionTest, ADDE_UpdateCR)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0;
    cpu.m_gpr[4] = 0;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 1;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[1], 0);
    EXPECT_EQ(cpu.m_cr.cr0.so, cpu.m_xer.so);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDIS TESTS
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDIS_ZeroBase)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0x1234;

    Core::Instruction::ADDIS(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0x1234 << 16);
}

TEST(InstructionTest, ADDIS_WithBaseRegister)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x1000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0x1111;

    Core::Instruction::ADDIS(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0x1000 + (0x1111 << 16));
}

TEST(InstructionTest, ADDIS_SignExtended)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;

    inst.si = static_cast<int32_t>(0xFFFF);

    Core::Instruction::ADDIS(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0xFFFF);
}
