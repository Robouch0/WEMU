/*
** EPITECH PROJECT, 2025
** core
** File description:
** test_eieio
*/

#include "TestFixture.hpp"

//
// ─────────────────────────────────────────────────────────────────────────────
//  EIEIO — no-op in single-threaded emulator: executes without side effects
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, EIEIO_ExecutesWithoutSideEffects)
{
    cpu->m_gpr[1] = 0xDEADBEEF;

    EncodedInstruction inst(0);

    EXPECT_NO_THROW(Core::Instruction::EIEIO(*cpu, inst));
    EXPECT_EQ(cpu->m_gpr[1], 0xDEADBEEFu); // no register modified
}
