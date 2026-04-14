// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_extsh
// */
// 
// #include "TestFixture.hpp"
// 
// // EXTSH: rA = EXTS(rS[16:31])  — sign-extend the low 16 bits of rS to 32 bits
// // Fields: inst.rt=RS(source), inst.ra=RA(dest)
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — positive small value: sign bit clear, no extension
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_PositiveSmall)
// {
//     cpu->m_gpr[3] = 0x00000001;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — max positive halfword: 0x7FFF → 0x00007FFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_MaxPositive)
// {
//     cpu->m_gpr[3] = 0x00007FFF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0x00007FFFu);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — min negative halfword: 0x8000 → 0xFFFF8000
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_MinNegative)
// {
//     cpu->m_gpr[3] = 0x00008000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8000u);
//     EXPECT_EQ(cpu->m_gprSigned[4], -32768);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — all halfword bits set: 0xFFFF → 0xFFFFFFFF (-1)
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_AllOnes)
// {
//     cpu->m_gpr[3] = 0x0000FFFF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
//     EXPECT_EQ(cpu->m_gprSigned[4], -1);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — zero: stays zero
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_Zero)
// {
//     cpu->m_gpr[3] = 0x00000000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — high 16 bits of rS are ignored
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_HighBitsIgnored)
// {
//     cpu->m_gpr[3] = 0xABCD1234; // high 16 bits must be ignored; low 16 = 0x1234
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0x00001234u); // 0x1234 is positive, no sign extension
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — high bits ignored even when low halfword is negative
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_HighBitsIgnored_NegativeLowHalf)
// {
//     cpu->m_gpr[3] = 0x00008001; // low 16 = 0x8001 → negative
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xFFFF8001u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — RC=1 updates CR0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_RC_UpdatesCR0_Negative)
// {
//     cpu->m_gpr[3] = 0x0000FFFF; // sign-extends to -1
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rc = 1;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xFFFFFFFFu);
//     EXPECT_EQ(cpu->m_cr.cr0.lt, 1u);
//     EXPECT_EQ(cpu->m_cr.cr0.gt, 0u);
//     EXPECT_EQ(cpu->m_cr.cr0.eq, 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  EXTSH — RS is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, EXTSH_DoesNotModifyRS)
// {
//     cpu->m_gpr[3] = 0x0000ABCD;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
// 
//     Core::Instruction::EXTSH(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[3], 0x0000ABCDu); // RS unchanged
// }
