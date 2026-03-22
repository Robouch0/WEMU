#include "TestFixture.hpp"

// TODO: add ADDIC / ADDIC. tests
#include "TestFixture.hpp"

//
// ───────────────────────────────────────────────────────────────
//  ADDIC OPCODE 12
// ───────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, ADDIC_NoCarry)
{
    cpu->m_gpr[3] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 50;

    Core::Instruction::ADDIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 150u);
    EXPECT_EQ(cpu->m_xer.ca, 0);
}

TEST_F(InstructionTest, ADDIC_WithCarry)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = 1;

    Core::Instruction::ADDIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

TEST_F(InstructionTest, ADDIC_NegativeImmediate)
{
    cpu->m_gpr[3] = 100;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = -50;

    Core::Instruction::ADDIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 50u);
    EXPECT_EQ(cpu->m_xer.ca, 1);
}

TEST_F(InstructionTest, ADDIC_NegativeImmediate_NoCarry)
{
    cpu->m_gpr[3] = 0;

    EncodedInstruction inst(0);
    inst.rt = 4;
    inst.ra = 3;
    inst.si = -1;

    Core::Instruction::ADDIC(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
    EXPECT_EQ(cpu->m_xer.ca, 0);
}
