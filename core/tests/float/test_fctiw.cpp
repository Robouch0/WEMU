// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_fctiw
// */
// 
// #include "TestFixture.hpp"
// #include <cstring>
// #include <cmath>
// 
// // FCTIW: FRT = (int32_t)FRB  — convert FPR to integer using FPSCR rounding mode
// // The integer result is stored in the low 32 bits of FRT (as bit pattern).
// // Fields: inst.rt=FRT(dest), inst.rb=FRB(source)
// 
// static int32_t fprToInt(float fprVal)
// {
//     int32_t result;
//     std::memcpy(&result, &fprVal, sizeof(result));
//     return result;
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — convert 1.0 → 1
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_One)
// {
//     cpu->m_fpr[2] = 1.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     EXPECT_EQ(fprToInt(cpu->m_fpr[4]), 1);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — convert -1.0 → -1
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_NegativeOne)
// {
//     cpu->m_fpr[2] = -1.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     EXPECT_EQ(fprToInt(cpu->m_fpr[4]), -1);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — convert 0.0 → 0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_Zero)
// {
//     cpu->m_fpr[2] = 0.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     EXPECT_EQ(fprToInt(cpu->m_fpr[4]), 0);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — convert 100.0 → 100
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_PositiveLarge)
// {
//     cpu->m_fpr[2] = 100.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     EXPECT_EQ(fprToInt(cpu->m_fpr[4]), 100);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — convert -100.0 → -100
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_NegativeLarge)
// {
//     cpu->m_fpr[2] = -100.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     EXPECT_EQ(fprToInt(cpu->m_fpr[4]), -100);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — convert 1.5 → rounds per FPSCR mode (default: round-to-nearest → 2)
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_RoundHalf_ToNearest)
// {
//     cpu->m_fpr[2] = 1.5f;
//     // FPSCR rounding mode = 0 (round to nearest, ties to even)
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     // Round-to-nearest: 1.5 → 2
//     EXPECT_EQ(fprToInt(cpu->m_fpr[4]), 2);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FCTIW — FRB is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FCTIW_DoesNotModifyFRB)
// {
//     cpu->m_fpr[2] = 42.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FCTIW(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[2], 42.0f);
// }
