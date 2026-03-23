#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ORI
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ORI)
{
    cpu->m_gpr[3] = 1768428544;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 38656;

    Core::Instruction::ORI(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 1768462080);
}
