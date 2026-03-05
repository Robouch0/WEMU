/*
** EPITECH PROJECT, 2025
** core
** File description:
** Unit tests for branch instructions
*/

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
//  B — unconditional branch (relative)
// ───────────────────────────────────────────────
//

TEST(BranchTest, B_Forward)
{
    auto cpu = makeCPU();
    cpu.m_pc = 0x100;

    EncodedInstruction inst(0);
    inst.li = 2; // offset = 2 << 2 = +8

    Core::Instruction::B(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x108u);
}

TEST(BranchTest, B_Backward)
{
    auto cpu = makeCPU();
    cpu.m_pc = 0x200;

    EncodedInstruction inst(0);
    inst.li = 0xFFFFFE; // sign-extended: -8

    Core::Instruction::B(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x1F8u);
}

//
// ───────────────────────────────────────────────
//  BA — unconditional branch (absolute)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BA_Absolute)
{
    auto cpu = makeCPU();

    EncodedInstruction inst(0);
    inst.li = 0x1000; // absolute target = 0x1000 << 2 = 0x4000

    Core::Instruction::BA(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x4000u);
}

//
// ───────────────────────────────────────────────
//  BL — branch and link (relative)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BL_SetsLR)
{
    auto cpu = makeCPU();
    cpu.m_pc = 0x100;

    EncodedInstruction inst(0);
    inst.li = 2; // offset = +8

    Core::Instruction::BL(cpu, inst);

    EXPECT_EQ(cpu.m_lr,     0x104u); // pc + 4
    EXPECT_EQ(cpu.m_nextPc, 0x108u); // pc + 8
}

//
// ───────────────────────────────────────────────
//  BLA — branch and link (absolute)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BLA_SetsLRAndAbsolute)
{
    auto cpu = makeCPU();
    cpu.m_pc = 0x200;

    EncodedInstruction inst(0);
    inst.li = 0x800; // absolute = 0x800 << 2 = 0x2000

    Core::Instruction::BLA(cpu, inst);

    EXPECT_EQ(cpu.m_lr,     0x204u);
    EXPECT_EQ(cpu.m_nextPc, 0x2000u);
}

//
// ───────────────────────────────────────────────
//  BLR — branch to link register
// ───────────────────────────────────────────────
//

TEST(BranchTest, BLR_JumpsToLR)
{
    auto cpu = makeCPU();
    cpu.m_lr = 0x12345678u;

    EncodedInstruction inst(0);

    Core::Instruction::BLR(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x12345678u);
}

TEST(BranchTest, BLR_ClearsLow2Bits)
{
    auto cpu = makeCPU();
    cpu.m_lr = 0x12345679u; // low 2 bits should be masked

    EncodedInstruction inst(0);

    Core::Instruction::BLR(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x12345678u);
}

//
// ───────────────────────────────────────────────
//  BC — conditional branch (relative)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BC_Taken_CRSet)
{
    auto cpu = makeCPU();
    cpu.m_pc      = 0x100;
    cpu.m_nextPc  = 0x104;
    cpu.m_cr.raw  = 0xFFFFFFFFu; // all CR bits set

    EncodedInstruction inst(0);
    inst.bo = 18; // 0b10010: branch if CR[BI] == 1
    inst.bi = 5;  // bit 5 of CR.raw
    inst.bd = 2;  // offset = 2 << 2 = +8

    Core::Instruction::BC(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x108u);
}

TEST(BranchTest, BC_NotTaken_CRClear)
{
    auto cpu = makeCPU();
    cpu.m_pc     = 0x100;
    cpu.m_nextPc = 0x104;
    cpu.m_cr.raw = 0u; // all CR bits clear

    EncodedInstruction inst(0);
    inst.bo = 18; // branch if CR[BI] == 1 → not satisfied
    inst.bi = 5;
    inst.bd = 2;

    Core::Instruction::BC(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x104u); // unchanged
}

TEST(BranchTest, BC_CTR_Taken)
{
    auto cpu = makeCPU();
    cpu.m_pc     = 0x100;
    cpu.m_nextPc = 0x104;
    cpu.m_ctr    = 3;

    EncodedInstruction inst(0);
    inst.bo = 4; // 0b00100: decrement CTR, branch if CTR != 0
    inst.bi = 0;
    inst.bd = 2;

    Core::Instruction::BC(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x108u);
    EXPECT_EQ(cpu.m_ctr,    2u); // decremented
}

TEST(BranchTest, BC_CTR_NotTaken)
{
    auto cpu = makeCPU();
    cpu.m_pc     = 0x100;
    cpu.m_nextPc = 0x104;
    cpu.m_ctr    = 1; // will decrement to 0 → not taken

    EncodedInstruction inst(0);
    inst.bo = 4;
    inst.bi = 0;
    inst.bd = 2;

    Core::Instruction::BC(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x104u); // unchanged
    EXPECT_EQ(cpu.m_ctr,    0u);     // decremented
}

//
// ───────────────────────────────────────────────
//  BCA — conditional branch (absolute)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BCA_Absolute)
{
    auto cpu = makeCPU();

    EncodedInstruction inst(0);
    inst.bo = 20;    // 0b10100: always branch
    inst.bi = 0;
    inst.bd = 0x400; // absolute = int16_t(0x400 << 2) = int16_t(0x1000) = 4096

    Core::Instruction::BCA(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x1000u);
}

//
// ───────────────────────────────────────────────
//  BCL — conditional branch and link (relative)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BCL_SetsLRWhenTaken)
{
    auto cpu = makeCPU();
    cpu.m_pc = 0x100;

    EncodedInstruction inst(0);
    inst.bo = 20; // always branch
    inst.bi = 0;
    inst.bd = 2;

    Core::Instruction::BCL(cpu, inst);

    EXPECT_EQ(cpu.m_lr,     0x104u);
    EXPECT_EQ(cpu.m_nextPc, 0x108u);
}

//
// ───────────────────────────────────────────────
//  BCLA — conditional branch and link (absolute)
// ───────────────────────────────────────────────
//

TEST(BranchTest, BCLA_SetsLRAndAbsolute)
{
    auto cpu = makeCPU();
    cpu.m_pc = 0x200;

    EncodedInstruction inst(0);
    inst.bo = 20;    // always branch
    inst.bi = 0;
    inst.bd = 0x100; // absolute = int16_t(0x100 << 2) = 0x400

    Core::Instruction::BCLA(cpu, inst);

    EXPECT_EQ(cpu.m_lr,     0x204u);
    EXPECT_EQ(cpu.m_nextPc, 0x400u);
}

//
// ───────────────────────────────────────────────
//  BCCTR — branch to count register conditionally
// ───────────────────────────────────────────────
//

TEST(BranchTest, BCCTR_Taken)
{
    auto cpu = makeCPU();
    cpu.m_ctr = 0x5000u;

    EncodedInstruction inst(0);
    inst.bo = 20; // always branch
    inst.bi = 0;

    Core::Instruction::BCCTR(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x5000u);
}

TEST(BranchTest, BCCTR_NotTaken_CRClear)
{
    auto cpu = makeCPU();
    cpu.m_nextPc = 0x200;
    cpu.m_ctr    = 0x5000u;
    cpu.m_cr.raw = 0u;

    EncodedInstruction inst(0);
    inst.bo = 18; // branch if CR[BI] == 1 → not satisfied (CR clear)
    inst.bi = 5;

    Core::Instruction::BCCTR(cpu, inst);

    EXPECT_EQ(cpu.m_nextPc, 0x200u); // unchanged
}

TEST(BranchTest, BCCTR_NeverDecrementsCTR)
{
    auto cpu = makeCPU();
    cpu.m_ctr = 5u;

    EncodedInstruction inst(0);
    inst.bo = 20; // always branch
    inst.bi = 0;

    Core::Instruction::BCCTR(cpu, inst);

    EXPECT_EQ(cpu.m_ctr, 5u); // BCCTR never decrements CTR
}
