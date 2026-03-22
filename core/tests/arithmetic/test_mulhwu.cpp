#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  MULHWU (RC=0)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULHWU_NoRc_LowProduct)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
}

TEST_F(InstructionTest, MULHWU_NoRc_HighProduct)
{
    cpu->m_gpr[3] = 45987;
    cpu->m_gpr[4] = 653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
}

//
// ───────────────────────────────────────────────
//  MULHWU (RC=1)
// ───────────────────────────────────────────────
//

TEST_F(InstructionTest, MULHWU_WithRc_LowProduct)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 10;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 0);
}

TEST_F(InstructionTest, MULHWU_WithRc_HighProduct)
{
    cpu->m_gpr[3] = 45987;
    cpu->m_gpr[4] = 653293;

    EncodedInstruction inst(0);
    inst.rt = 5;
    inst.ra = 3;
    inst.rb = 4;
    inst.rc = 0;

    Core::Instruction::MULHWU(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 6);
}