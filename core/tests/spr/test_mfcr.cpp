// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_mfcr
// */
// 
// #include "TestFixture.hpp"
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — basic read: copies full CR into RT
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_BasicRead)
// {
//     cpu->m_cr.raw = 0xA5000000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xA5000000u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — CR=0 → RT=0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_CRZero)
// {
//     cpu->m_cr.raw = 0;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 5;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[5], 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — CR=0xFFFFFFFF → RT=0xFFFFFFFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_CRAllOnes)
// {
//     cpu->m_cr.raw = 0xFFFFFFFF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 6;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[6], 0xFFFFFFFFu);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — RT=r0 is allowed
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_RT0_Allowed)
// {
//     cpu->m_cr.raw = 0x12345678;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 0;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[0], 0x12345678u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — CR is not modified by the read
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_DoesNotModifyCR)
// {
//     cpu->m_cr.raw = 0xDEAD0000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_cr.raw, 0xDEAD0000u); // CR unchanged
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — specific CR fields reflected correctly
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_SpecificFieldsReflected)
// {
//     // Set CR0: LT=1, GT=0, EQ=0, SO=0 — and clear everything else
//     cpu->m_cr.raw = 0;
//     cpu->m_cr.cr0.lt = 1;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     // CR0 occupies the high nibble of the 32-bit CR word
//     EXPECT_NE(cpu->m_gpr[4], 0u); // at least the LT bit is set
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  MFCR — does not affect other GPRs
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, MFCR_DoesNotModifyOtherGPRs)
// {
//     cpu->m_cr.raw = 0xABCD1234;
//     cpu->m_gpr[7] = 0x99999999;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
// 
//     Core::Instruction::MFCR(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[7], 0x99999999u); // unrelated GPR unchanged
// }
