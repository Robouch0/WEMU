#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  MULLI
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULLI_Positive)
{
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 10;

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_NegativeRA)
{
    cpu->m_gprSigned[3] = -5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 10;

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_NegativeImm)
{
    cpu->m_gprSigned[3] = 5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = static_cast<uint16_t>(-10);

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_BothNegative)
{
    cpu->m_gprSigned[3] = -5;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = static_cast<uint16_t>(-10);

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 50);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_Zero_RA)
{
    cpu->m_gprSigned[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 12345;

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_Zero_Imm)
{
    cpu->m_gprSigned[3] = 12345;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0;

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], 0);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_Overflow_KeepsLow32Bits)
{
    cpu->m_gprSigned[3] = INT32_MAX;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 2;

    Core::Instruction::MULLI(*cpu, inst);

    // INT32_MAX * 2 = 0x1FFFFFFFE, low 32 bits = 0xFFFFFFFE
    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFEu);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_MaxImm)
{
    cpu->m_gprSigned[3] = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = INT16_MAX;

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], INT16_MAX);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULLI_MinImm)
{
    cpu->m_gprSigned[3] = 1;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = static_cast<uint16_t>(INT16_MIN);

    Core::Instruction::MULLI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -32768);
    EXPECT_EQ(cpu->m_xer.ov, 0);
    EXPECT_EQ(cpu->m_xer.so, 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
