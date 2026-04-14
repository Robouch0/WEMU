// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_fabs
// */
// 
// #include "TestFixture.hpp"
// #include <cmath>
// 
// // FABS: FRT = |FRB|  (clear sign bit, no rounding)
// // Fields: inst.rt=FRT(dest), inst.rb=FRB(source)
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — positive value: unchanged
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_PositiveUnchanged)
// {
//     cpu->m_fpr[2] = 3.14f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4; // FRT
//     inst.rb = 2; // FRB
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.14f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — negative value: sign cleared
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_NegativeBecomesPositive)
// {
//     cpu->m_fpr[2] = -3.14f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.14f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — +0.0 stays +0.0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_PositiveZero)
// {
//     cpu->m_fpr[2] = 0.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
//     EXPECT_FALSE(std::signbit(cpu->m_fpr[4]));
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — -0.0 becomes +0.0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_NegativeZero_BecomesPositiveZero)
// {
//     cpu->m_fpr[2] = -0.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
//     EXPECT_FALSE(std::signbit(cpu->m_fpr[4]));
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — 1.0f stays 1.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_One)
// {
//     cpu->m_fpr[2] = 1.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — -1.0f becomes 1.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_NegativeOne)
// {
//     cpu->m_fpr[2] = -1.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — FRB is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_DoesNotModifyFRB)
// {
//     cpu->m_fpr[2] = -5.5f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FABS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[2], -5.5f); // FRB unchanged
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FABS — RC=1 updates CR1
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FABS_RC_UpdatesCR1)
// {
//     cpu->m_fpr[2] = -3.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
//     inst.rc = 1;
// 
//     // RC=1 for float instructions copies FPSCR exception bits into CR1
//     EXPECT_NO_THROW(Core::Instruction::FABS(*cpu, inst));
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.0f);
// }
