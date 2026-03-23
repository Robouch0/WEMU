#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  XORI
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, XORI)
{
    cpu->m_gpr[3] = 1768428544;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 65535;

    Core::Instruction::XORI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1768483839);
}
