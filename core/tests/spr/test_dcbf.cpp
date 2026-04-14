// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_dcbf
// */
// 
// #include "TestFixture.hpp"
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  DCBF — no-op in single-threaded emulator: executes without side effects
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, DCBF_ExecutesWithoutSideEffects)
// {
//     cpu->m_gpr[1] = 0xDEADBEEF;
// 
//     EncodedInstruction inst(0);
//     inst.ra = 1;
//     inst.rb = 2;
// 
//     EXPECT_NO_THROW(Core::Instruction::DCBF(*cpu, inst));
//     EXPECT_EQ(cpu->m_gpr[1], 0xDEADBEEFu); // no register modified
// }
