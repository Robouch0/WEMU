#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  EXTSH (RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSH_NoRc_Positive)
{
    cpu->m_gpr[3] = 0x00007FFF; // bit 15 = 0 → pas d'extension de signe

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00007FFF);
}

TEST_F(InstructionTest, EXTSH_NoRc_Negative)
{
    cpu->m_gpr[3] = 0x0000FFFF; // bit 15 = 1 → extension de signe

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFF);
}

TEST_F(InstructionTest, EXTSH_NoRc_Zero)
{
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00000000);
}

TEST_F(InstructionTest, EXTSH_NoRc_UpperBytesIgnored)
{
    cpu->m_gpr[3] = 0xABCD1234; // octets hauts ignorés, bit 15 du mot bas = 0

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00001234);
}

//
// ───────────────────────────────────────────────
//  EXTSH. (RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSH_WithRc_Greater)
{
    cpu->m_gpr[3] = 0x00001234;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00001234);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, EXTSH_WithRc_Zero)
{
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00000000);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, EXTSH_WithRc_Less)
{
    cpu->m_gpr[3] = 0x0000FFFF; // extension de signe → 0xFFFFFFFF → -1

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::EXTSH(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFF);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
