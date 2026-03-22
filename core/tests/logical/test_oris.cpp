#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  ORIS
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, ORIS)
{
    cpu->m_gpr[3] = 1768428544;
    cpu->m_gpr[4] = 38656;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 4;

    Core::Instruction::ORIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 4285010944);
}
