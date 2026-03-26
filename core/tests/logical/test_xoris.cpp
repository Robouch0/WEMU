#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  XORIS
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, XORIS)
{
    cpu->m_gpr[3] = 1768428544;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rs = 3;
    inst.ui = 65535;

    Core::Instruction::XORIS(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 2526483456);
}
