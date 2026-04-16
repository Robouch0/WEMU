// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_sync
// */
// 
// #include "TestFixture.hpp"
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  SYNC — no-op in single-threaded emulator: executes without side effects
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, SYNC_ExecutesWithoutSideEffects)
// {
//     cpu->m_gpr[1] = 0xDEADBEEF;
// 
//     EncodedInstruction inst(0);
// 
//     EXPECT_NO_THROW(Core::Instruction::SYNC(*cpu, inst));
//     EXPECT_EQ(cpu->m_gpr[1], 0xDEADBEEFu); // no register modified
// }
