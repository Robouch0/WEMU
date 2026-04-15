// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lfs
// */
// 
// #include "TestFixture.hpp"
// #include <cstring>
// 
// static constexpr uint32_t TEST_ADDR = 0x02000200;
// 
// // LFS: FRT = float loaded from EA (32-bit IEEE 754)
// // EA = (RA|0) + EXTS(D). RA=0 means base is 0 (not r0).
// // Fields: inst.rt=FRT, inst.ra=RA, inst.si=D
// 
// static void writeFloat(Core::Interpreter *cpu, uint32_t addr, float val)
// {
//     uint32_t bits;
//     std::memcpy(&bits, &val, sizeof(bits));
//     cpu->m_memory.write<uint32_t>(addr, bits);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — basic load of 1.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_BasicLoad_One)
// {
//     writeFloat(cpu, TEST_ADDR, 1.0f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — load -1.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_LoadNegativeOne)
// {
//     writeFloat(cpu, TEST_ADDR, -1.0f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], -1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — load 0.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_LoadZero)
// {
//     writeFloat(cpu, TEST_ADDR, 0.0f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 0.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — positive displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_PositiveDisplacement)
// {
//     writeFloat(cpu, TEST_ADDR + 8, 3.14f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 8;
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 3.14f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — negative displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_NegativeDisplacement)
// {
//     writeFloat(cpu, TEST_ADDR, -2.5f);
//     cpu->m_gpr[1] = TEST_ADDR + 16;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], -2.5f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — RA=0 uses 0 as base (not r0), unmapped → exception
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_RA0_Uses0NotR0)
// {
//     cpu->m_gpr[0] = TEST_ADDR; // r0 must be ignored
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.si = 0;
// 
//     EXPECT_THROW(Core::Instruction::LFS(*cpu, inst), Core::MemoryException);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_DoesNotUpdateRA)
// {
//     writeFloat(cpu, TEST_ADDR, 1.0f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR); // RA unchanged
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — successive loads to different FPRs
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_SuccessiveLoads)
// {
//     writeFloat(cpu, TEST_ADDR,     1.5f);
//     writeFloat(cpu, TEST_ADDR + 4, 2.5f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst1(0);
//     inst1.rt = 3; inst1.ra = 1; inst1.si = 0;
//     Core::Instruction::LFS(*cpu, inst1);
// 
//     EncodedInstruction inst2(0);
//     inst2.rt = 5; inst2.ra = 1; inst2.si = 4;
//     Core::Instruction::LFS(*cpu, inst2);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[3], 1.5f);
//     EXPECT_FLOAT_EQ(cpu->m_fpr[5], 2.5f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — FRT=f0 is allowed
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_FRT0_Allowed)
// {
//     writeFloat(cpu, TEST_ADDR, 4.0f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 0; // FRT = f0
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::LFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[0], 4.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LFS — maximum positive displacement (0x7FFC, 4-byte aligned): no throw
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, LFS_MaxPositiveSI)
// {
//     writeFloat(cpu, TEST_ADDR + 0x7FFC, -1.0f);
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 5;
//     inst.ra = 1;
//     inst.si = 0x7FFC;
// 
//     EXPECT_NO_THROW(Core::Instruction::LFS(*cpu, inst));
//     EXPECT_FLOAT_EQ(cpu->m_fpr[5], -1.0f);
// }
