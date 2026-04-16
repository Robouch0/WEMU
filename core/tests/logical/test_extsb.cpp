#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  EXTSB (RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_NoRc_Positive)
{
    cpu->m_gpr[3] = 0x0000007F; // bit 7 = 0 → pas d'extension de signe

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x0000007F);
}

TEST_F(InstructionTest, EXTSB_NoRc_Negative)
{
    cpu->m_gpr[3] = 0x000000FF; // bit 7 = 1 → extension de signe

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFF);
}

TEST_F(InstructionTest, EXTSB_NoRc_Zero)
{
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00000000);
}

TEST_F(InstructionTest, EXTSB_NoRc_UpperBytesIgnored)
{
    cpu->m_gpr[3] = 0xABCD0042; // octets hauts ignorés, bit 7 de l'octet bas = 0

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00000042);
}

//
// ───────────────────────────────────────────────
//  EXTSB. (RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, EXTSB_WithRc_Greater)
{
    cpu->m_gpr[3] = 0x00000042; // résultat positif non nul

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00000042);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, EXTSB_WithRc_Zero)
{
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0x00000000);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, EXTSB_WithRc_Less)
{
    cpu->m_gpr[3] = 0x000000FF; // extension de signe → 0xFFFFFFFF → -1

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 5;
    inst.rc = 1;

    Core::Instruction::EXTSB(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0xFFFFFFFF);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}
