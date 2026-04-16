// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_rlwinm
// */
// 
// #include "TestFixture.hpp"
// 
// // RLWINM: rA = ROTL32(rS, SH) & MASK(MB, ME)
// // SH is a 5-bit immediate at inst.rb (C++ bits 11-15).
// // MB at C++ raw bits [6:10]: inst.raw |= (mb << 6)
// // ME at C++ raw bits [1:5]:  inst.raw |= (me << 1)
// 
// static uint32_t rotl32_i(uint32_t val, uint32_t sh)
// {
//     sh &= 31;
//     return (sh == 0) ? val : ((val << sh) | (val >> (32 - sh)));
// }
// 
// static uint32_t ppcMask_i(uint32_t mb, uint32_t me)
// {
//     if (mb <= me)
//         return ((0xFFFFFFFFu >> mb) & (0xFFFFFFFFu << (31 - me)));
//     return ~ppcMask_i(me + 1, mb - 1);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — basic: rotate by 8 with full mask (logical shift left by 8)
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_BasicRotate)
// {
//     cpu->m_gpr[3] = 0xFF000000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 8; // SH=8
//     inst.raw |= (0 << 6) | (31 << 1); // MB=0, ME=31
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0x00FF0000u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — SH=0: no rotation, only mask applied
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_SH0_JustMask)
// {
//     cpu->m_gpr[3] = 0xFFFFFFFF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 0; // SH=0
//     // MB=8, ME=23 → keep bits [8:23] (middle two bytes)
//     inst.raw |= (8 << 6) | (23 << 1);
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], ppcMask_i(8, 23));
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — extract high byte: SH=0, MB=0, ME=7
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_ExtractHighByte)
// {
//     cpu->m_gpr[3] = 0xABCDEF12;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 0; // SH=0
//     inst.raw |= (0 << 6) | (7 << 1); // MB=0, ME=7
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xAB000000u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — shift left 16 (SH=16, MB=0, ME=15)
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_ShiftLeft16)
// {
//     cpu->m_gpr[3] = 0x0000ABCD;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 16; // SH=16
//     inst.raw |= (0 << 6) | (15 << 1); // MB=0, ME=15
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0xABCD0000u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — SH=31: rotate right by 1 equivalent
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_SH31_RotateRight1)
// {
//     cpu->m_gpr[3] = 0x00000001;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 31; // SH=31 → rotl by 31 = rotr by 1
//     inst.raw |= (0 << 6) | (31 << 1); // full mask
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — shift right 16 logical (SH=16, MB=16, ME=31)
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_ShiftRight16_Logical)
// {
//     cpu->m_gpr[3] = 0xABCD0000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 16; // SH=16
//     // After rotl by 16: 0x0000ABCD; MB=16, ME=31 keeps low 16 bits
//     inst.raw |= (16 << 6) | (31 << 1);
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0x0000ABCDu);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — full mask (MB=0, ME=31): result equals pure rotation
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_FullMask_PureRotation)
// {
//     cpu->m_gpr[3] = 0x12345678;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 12; // SH=12
//     inst.raw |= (0 << 6) | (31 << 1);
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], rotl32_i(0x12345678, 12));
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — RC=1 updates CR0
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_RC_UpdatesCR0_Zero)
// {
//     cpu->m_gpr[3] = 0x00000000;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 4;
//     inst.raw |= (0 << 6) | (31 << 1);
//     inst.rc = 1;
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[4], 0u);
//     EXPECT_EQ(cpu->m_cr.cr0.eq, 1u);
//     EXPECT_EQ(cpu->m_cr.cr0.lt, 0u);
//     EXPECT_EQ(cpu->m_cr.cr0.gt, 0u);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  RLWINM — RS is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, RLWINM_DoesNotModifyRS)
// {
//     cpu->m_gpr[3] = 0xDEADBEEF;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 3;
//     inst.ra = 4;
//     inst.rb = 8;
//     inst.raw |= (0 << 6) | (31 << 1);
// 
//     Core::Instruction::RLWINM(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[3], 0xDEADBEEFu);
// }
