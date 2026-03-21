#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  ADDI
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDI_RAisZero)
{
    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 0;
    inst.si = 1234;

    Core::Instruction::ADDI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1234u);
}

TEST_F(InstructionTest, ADDI_Normal)
{
    cpu->m_gpr[3] = 1000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 200;

    Core::Instruction::ADDI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 1200u);
}

TEST_F(InstructionTest, ADDI_SignExtendedImmediate)
{
    cpu->m_gpr[6] = 1000;

    EncodedInstruction inst(0);
    inst.rt = 7;
    inst.ra = 6;
    inst.si = 0xFFFF; // -1

    Core::Instruction::ADDI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[7], 999u);
}

TEST_F(InstructionTest, ADDI_RA0_NegativeImmediate)
{
    EncodedInstruction inst(0);
    inst.rt = 2;
    inst.ra = 0;
    inst.si = 0xFF00; // -256

    Core::Instruction::ADDI(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[2], -256);
}

//
// ───────────────────────────────────────────────────────────────
//  ADDIS
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDIS_ZeroBase)
{
    cpu->m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0x1234;

    Core::Instruction::ADDIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x12340000u);
}

TEST_F(InstructionTest, ADDIS_WithBaseRegister)
{
    cpu->m_gpr[3] = 0x1000;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0x1111;

    Core::Instruction::ADDIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x1000u + (0x1111u << 16));
}

TEST_F(InstructionTest, ADDIS_SignExtended)
{
    cpu->m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 0xFFFF;

    Core::Instruction::ADDIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gprSigned[4], -65536);
}
