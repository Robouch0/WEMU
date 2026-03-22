#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  MULHW (RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULHW_NoRc_LowProduct)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
}

TEST_F(InstructionTest, MULHW_NoRc_HighProduct)
{
    cpu->m_gpr[3] = 45987;
    cpu->m_gpr[4] = 653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
}

TEST_F(InstructionTest, MULHW_NoRc_NegativeProduct)
{
    cpu->m_gpr[3] = -3;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4294967295);
}

//
// ───────────────────────────────────────────────
//  MULHW (RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULHW_WithRc_LowProduct)
{
    std::int32_t a = 5;
    std::int32_t b = 10;
    cpu->m_gpr[3] = static_cast<std::uint32_t>(a);
    cpu->m_gpr[4] = static_cast<std::uint32_t>(b);

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 1);
}

TEST_F(InstructionTest, MULHW_WithRc_HighProduct)
{
    cpu->m_gpr[3] = 45987;
    cpu->m_gpr[4] = 653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULHW_WithRc_NegativeProduct)
{
    cpu->m_gpr[3] = -3;
    cpu->m_gpr[4] = 2;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 1;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4294967295);
    EXPECT_EQ(cpu->m_cr.cr0.lt, 1);
    EXPECT_EQ(cpu->m_cr.cr0.gt, 0);
    EXPECT_EQ(cpu->m_cr.cr0.eq, 0);
}

TEST_F(InstructionTest, MULHW_NoRc_BothNegative)
{
    cpu->m_gprSigned[3] = -45987;
    cpu->m_gprSigned[4] = -653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHW(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
}

TEST_F(InstructionTest, MULHW_NoRc_MinMin)
{
    cpu->m_gprSigned[3] = INT32_MIN;
    cpu->m_gprSigned[4] = INT32_MIN;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHW(*cpu, inst);

    // INT32_MIN * INT32_MIN = 0x4000000000000000 >> 32 = 0x40000000
    EXPECT_EQ(cpu->m_gpr[5], 0x40000000u);
}
