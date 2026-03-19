
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
// ───────────────────────────────────────────────
//  ADD (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADD_NoOE_NoRc)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 32;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADD(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 42);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADD. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADD_WithRc_UpdateCR)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = -5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 8;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    std::cout << "check here!" << std::endl;

    Core::Instruction::ADD(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[8], 5);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
}

//
// ───────────────────────────────────────────────
//  ADDO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDO_WithOverflow_SetsOVandSO)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x7FFFFFFF;
    cpu.m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 2;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADD(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[2], 0x80000000u);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);

    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDO_WithOE_And_Rc)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x7FFFFFFF;
    cpu.m_gpr[4] = 0x7FFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 9;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADD(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[9], 0x7FFFFFFF + 0x7FFFFFFF);

    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);

    if (cpu.m_gprSigned[9] < 0) {
        EXPECT_EQ(cpu.m_cr.cr0.lt, 1);
        EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
        EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    }
}

//
// ───────────────────────────────────────────────
//  ADDE (OE=0, RC=0, CA=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDE_NoOE_NoRc_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 32;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 42);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDE (OE=0, RC=0, CA=1)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDE_NoOE_NoRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_gpr[4] = 32;
    cpu.m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 43);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDE. (OE=0, RC=1, CA=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDE_WithRc_UpdateCR)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = -5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 8;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[8], 5);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
}

//
// ───────────────────────────────────────────────
//  ADDEO (OE=1, RC=0, CA=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDEO_WithOverflow_SetsOVandSO)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x7FFFFFFF;
    cpu.m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 2;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDE(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[2], 0x80000000u);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);

    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDEO. (OE=1, RC=1, CA=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDEO_WithOE_And_Rc)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x7FFFFFFF;
    cpu.m_gpr[4] = 0x7FFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 9;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADD(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[9], 0x7FFFFFFF + 0x7FFFFFFF);

    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);

    if (cpu.m_gprSigned[9] < 0) {
        EXPECT_EQ(cpu.m_cr.cr0.lt, 1);
        EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
        EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    }
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME (OE=0, RC=0, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_NoOE_NoRc_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 9);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME (OE=0, RC=0, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_NoOE_NoRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 10;
    cpu.m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 10);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME. (OE=0, RC=1, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_NoOe_WithRc_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 52;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 51);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDME. (OE=0, RC=1, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_NoOe_WithRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 52;
    cpu.m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 52);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);

    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO (OE=1, RC=0, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_WithOe_NoRc_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0X80000000;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0x7FFFFFFF);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO (OE=1, RC=0, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_WithOe_NoRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 1;
    cpu.m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 1);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO. (OE=1, RC=1, CA=0)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_WithOe_WithRc_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0X80000000;
    cpu.m_xer.ca = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0x7FFFFFFF);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDMEO. (OE=1, RC=1, CA=1)
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDME_WithOe_WithRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0x80000000;
    cpu.m_xer.ca = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDME(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0x80000000);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);

    EXPECT_EQ(cpu.m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDI TESTS
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDI_RAisZero)
{
    auto cpu = makeCPU();

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 0;
    inst.si = 1234;

    Core::Instruction::ADDI(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 1234);
}

TEST(InstructionTest, ADDI_Normal)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 1000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 200;

    Core::Instruction::ADDI(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 1200);
}

TEST(InstructionTest, ADDI_SignExtendedImmediate)
{
    auto cpu = makeCPU();

    cpu.m_gpr[6] = 1000;

    EncodedInstruction inst(0);
    inst.rt = 7;
    inst.ra = 6;
    inst.si = 0xFFFF; // -1

    Core::Instruction::ADDI(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[7], 1000 - 1);
}

TEST(InstructionTest, ADDI_RA0_NegativeImmediate)
{
    auto cpu = makeCPU();

    EncodedInstruction inst(0);
    inst.rt = 2;
    inst.ra = 0;
    inst.si = 0xFF00; // -256

    Core::Instruction::ADDI(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[2], -256);
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
// ───────────────────────────────────────────────
//  ADDC (OE=0, RC=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDC_NoOE_NoRc_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 15);
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_NoOE_NoRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0xFFFFFFFF;
    cpu.m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_NoOE_NoRc_Zero)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0;
    cpu.m_gpr[4] = 0;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 0);
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDC. (OE=0, RC=1)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDC_NoOE_WithRc_Positive)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 15);
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_NoOE_WithRc_Negative)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = -10;
    cpu.m_gprSigned[4] = -5;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], -15);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_NoOE_WithRc_Zero)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = -5;
    cpu.m_gprSigned[4] = 5;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 1);
}

TEST(InstructionTest, ADDC_NoOE_WithRc_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0xFFFFFFFF;
    cpu.m_gpr[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 0;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[5], 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 1);
}

//
// ───────────────────────────────────────────────
//  ADDCO (OE=1, RC=0)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDC_WithOE_NoRc_NoOverflow)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 15);
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_WithOE_NoRc_PositiveOverflow)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = 0x7FFFFFFF;
    cpu.m_gprSigned[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], static_cast<int32_t>(0x80000000));
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_WithOE_NoRc_NegativeOverflow)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = static_cast<int32_t>(0x80000000);
    cpu.m_gprSigned[4] = -1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 0x7FFFFFFF);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_WithOE_NoRc_SOPersists)
{
    auto cpu = makeCPU();

    cpu.m_xer.so = 1;
    cpu.m_gpr[3] = 5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 0;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 15);
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

//
// ───────────────────────────────────────────────
//  ADDCO. (OE=1, RC=1)
// ───────────────────────────────────────────────
//

TEST(InstructionTest, ADDC_WithOE_WithRc_NoOverflow)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = -5;
    cpu.m_gprSigned[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 5);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 0);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
}

TEST(InstructionTest, ADDC_WithOE_WithRc_PositiveOverflow)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = 0x7FFFFFFF;
    cpu.m_gprSigned[4] = 1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], static_cast<int32_t>(0x80000000));
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_cr.cr0.so, 1);
}

TEST(InstructionTest, ADDC_WithOE_WithRc_NegativeOverflow)
{
    auto cpu = makeCPU();

    cpu.m_gprSigned[3] = static_cast<int32_t>(0x80000000);
    cpu.m_gprSigned[4] = -1;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 0x7FFFFFFF);
    EXPECT_EQ(cpu.m_xer.ca, 1);
    EXPECT_EQ(cpu.m_xer.ov, 1);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_cr.cr0.so, 1);
}

TEST(InstructionTest, ADDC_WithOE_WithRc_SOReflectedInCR0)
{
    auto cpu = makeCPU();

    cpu.m_xer.so = 1;
    cpu.m_gpr[3] = 5;
    cpu.m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.oe = 1;
    inst.rc = 1;

    Core::Instruction::ADDC(cpu, inst);

    EXPECT_EQ(cpu.m_gprSigned[5], 15);
    EXPECT_EQ(cpu.m_xer.ca, 0);
    EXPECT_EQ(cpu.m_xer.ov, 0);
    EXPECT_EQ(cpu.m_xer.so, 1);
    EXPECT_EQ(cpu.m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu.m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu.m_cr.cr0.eq, 0);
    EXPECT_EQ(cpu.m_cr.cr0.so, 1);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDIC OPCODE 12
// ───────────────────────────────────────────────────────────────
//

TEST(InstructionTest, ADDIC_NoCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 50;

    Core::Instruction::ADDIC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 150);
    EXPECT_EQ(cpu.m_xer.ca, 0);
}

TEST(InstructionTest, ADDIC_WithCarry)
{
    auto cpu = makeCPU();

    cpu.m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 1;

    Core::Instruction::ADDIC(cpu, inst);

    EXPECT_EQ(cpu.m_gpr[4], 0);
    EXPECT_EQ(cpu.m_xer.ca, 1);
}
