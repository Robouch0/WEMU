// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_cntlzw
// */
// 
// #include "TestFixture.hpp"
// 
// // CNTLZW: rA = count of leading zeros in rS (32-bit word)
// // Fields: inst.rt=RS(source), inst.ra=RA(dest)
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — all zeros: result is 32
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_AllZeros)
// {
//     cpu->m_gpr[3] = 0x00000000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 32u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — all ones: result is 0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_AllOnes)
// {
//     cpu->m_gpr[3] = 0xFFFFFFFF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — MSB set: result is 0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_MSBSet)
// {
//     cpu->m_gpr[3] = 0x80000000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — bit 30 set (second bit): result is 1
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_Bit30Set)
// {
//     cpu->m_gpr[3] = 0x40000000; // bit 30 in PPC notation (2nd from MSB)
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 1u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — only LSB set: result is 31
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_OnlyLSBSet)
// {
//     cpu->m_gpr[3] = 0x00000001;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 31u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — bit 16 set (mid-point): result is 15
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_Bit16Set)
// {
//     cpu->m_gpr[3] = 0x00010000; // bit 16 from MSB
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 15u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — 0x00FFFFFF: 8 leading zeros
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_8LeadingZeros)
// {
//     cpu->m_gpr[3] = 0x00FFFFFF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 8u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — RC=1 updates CR0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_RC_UpdatesCR0)
// {
//     cpu->m_gpr[3] = 0x00000000; // result will be 32
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rc = 1;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 32u);
//     EXPECT_EQ(cpu->m_cr.cr0.lt, 0u); // 32 is positive
//     EXPECT_EQ(cpu->m_cr.cr0.gt, 1u);
//     EXPECT_EQ(cpu->m_cr.cr0.eq, 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  CNTLZW — RS is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, CNTLZW_DoesNotModifyRS)
// {
//     cpu->m_gpr[3] = 0x00FF0000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::CNTLZW(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[3], 0x00FF0000u); // RS unchanged
// }
