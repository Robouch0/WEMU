#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────
//  XOR
// ───────────────────────────────────────────────
//


TEST_F(InstructionTest, XOR)
{
    cpu->m_gpr[3] = 5;
    cpu->m_gpr[4] = 9;

    EncodedInstruction inst(0);
    inst.ra = 5;
    inst.rb = 4;
    inst.rs = 3;

    Core::Instruction::XOR(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[5], 12);
}
