// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_dcbt
// */
//
// #include "TestFixture.hpp"
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  DCBT — no-op in single-threaded emulator: executes without side effects
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, DCBT_ExecutesWithoutSideEffects)
// {
//     cpu->m_gpr[1] = 0xDEADBEEF;
//
//     EncodedInstruction inst(0);
//     inst.ra = 1;
//     inst.rb = 2;
//
//     EXPECT_NO_THROW(Core::Instruction::DCBT(*cpu, inst));
//     EXPECT_EQ(cpu->m_gpr[1], 0xDEADBEEFu); // no register modified
// }
