// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_frsp
// */
// 
// #include "TestFixture.hpp"
// 
// // FRSP: FRT = (float)FRB  — round double-precision FRB to single precision
// // Since m_fpr is float[], this is effectively a no-op or a precision clamp.
// // Fields: inst.rt=FRT(dest), inst.rb=FRB(source)
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FRSP — basic: value exactly representable in float is preserved
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FRSP_ExactlyRepresentable)
// {
//     cpu->m_fpr[2] = 1.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FRSP(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FRSP — negative value preserved
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FRSP_Negative)
// {
//     cpu->m_fpr[2] = -1.5f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FRSP(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], -1.5f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FRSP — zero stays zero
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FRSP_Zero)
// {
//     cpu->m_fpr[2] = 0.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FRSP(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FRSP — max float value is preserved
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FRSP_MaxFloat)
// {
//     cpu->m_fpr[2] = 3.4028235e38f; // FLT_MAX
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FRSP(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.4028235e38f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FRSP — FRB is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FRSP_DoesNotModifyFRB)
// {
//     cpu->m_fpr[2] = 2.71828f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 2;
// 
//     Core::Instruction::FRSP(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[2], 2.71828f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  FRSP — FRT=FRB (in-place): allowed
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, FRSP_InPlace)
// {
//     cpu->m_fpr[4] = -3.0f;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.rb = 4;
// 
//     Core::Instruction::FRSP(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], -3.0f);
// }
