
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

    inst.si = 0xFFFF;

    Core::Instruction::ADDIS(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], -65536);
}

//
// ────────────────────────────────────────────────
//  ADDC TESTS
// ────────────────────────────────────────────────
//

TEST(InstructionTest, ADDC_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 20;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 30);
    EXPECT_EQ(cpu.m_xer.ca, 0);
}

TEST(InstructionTest, ADDC_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0xFFFFFFFFu;
    cpu.m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 6;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[6], 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ────────────────────────────────────────────────
//  ADDCO TESTS
// ────────────────────────────────────────────────
//

TEST(InstructionTest, ADDCO_OverflowSetsSOandOV)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x7FFFFFFF;
    cpu.m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDCO(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 0x80000000u);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
}

TEST(InstructionTest, ADDCO_UpdateCR)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = -20;

    EncodedInstruction inst(0);
    inst.rt = 8;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDCO(cpu, inst);

    int32_t result = cpu.m_gprSigned[8];

    EXPECT_EQ(result, -10);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDCO_CarryOut)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0xFFFFFFFFu;
    cpu.m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 7;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDCO(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[7], 1);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}
