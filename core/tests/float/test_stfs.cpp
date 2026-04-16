// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stfs
// */
// 
// #include "TestFixture.hpp"
// #include <cstring>
// 
// static constexpr uint32_t TEST_ADDR = 0x02000200;
// 
// // STFS: MEM(EA, 4) = FRS stored as 32-bit IEEE 754 float
// // EA = (RA|0) + EXTS(D). RA=0 means base is 0.
// // Fields: inst.rt=FRS, inst.ra=RA, inst.si=D
// 
// static float readFloat(Core::Interpreter *cpu, uint32_t addr)
// {
//     uint32_t bits = cpu->m_memory.read<uint32_t>(addr);
//     float val;
//     std::memcpy(&val, &bits, sizeof(val));
//     return val;
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — basic store of 1.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_BasicStore_One)
// {
//     cpu->m_fpr[4] = 1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4; // FRS
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR), 1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — store -1.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_StoreNegativeOne)
// {
//     cpu->m_fpr[4] = -1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR), -1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — store 0.0f
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_StoreZero)
// {
//     cpu->m_fpr[4] = 0.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR), 0.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — positive displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_PositiveDisplacement)
// {
//     cpu->m_fpr[4] = 3.14f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 8;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR + 8), 3.14f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — negative displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_NegativeDisplacement)
// {
//     cpu->m_fpr[4] = -2.5f;
//     cpu->m_gpr[1] = TEST_ADDR + 16;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR), -2.5f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — RA=0 uses 0 as base → exception
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_RA0_Uses0NotR0)
// {
//     cpu->m_fpr[4] = 1.0f;
//     cpu->m_gpr[0] = TEST_ADDR; // r0 must be ignored
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.si = 0;
// 
//     EXPECT_THROW(Core::Instruction::STFS(*cpu, inst), Core::MemoryException);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_DoesNotUpdateRA)
// {
//     cpu->m_fpr[4] = 1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — FPR source is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_DoesNotModifyFPR)
// {
//     cpu->m_fpr[4] = 1.5f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], 1.5f); // FPR unchanged
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — successive stores to adjacent locations
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_SuccessiveStores)
// {
//     cpu->m_fpr[3] = 1.0f;
//     cpu->m_fpr[5] = -1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst1(0);
//     inst1.rt = 3; inst1.ra = 1; inst1.si = 0;
//     Core::Instruction::STFS(*cpu, inst1);
// 
//     EncodedInstruction inst2(0);
//     inst2.rt = 5; inst2.ra = 1; inst2.si = 4;
//     Core::Instruction::STFS(*cpu, inst2);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR),     1.0f);
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR + 4), -1.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — FRS=f0 is allowed as source
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_FRS0_Allowed)
// {
//     cpu->m_fpr[0] = 7.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 0; // FRS = f0
//     inst.ra = 1;
//     inst.si = 0;
// 
//     Core::Instruction::STFS(*cpu, inst);
// 
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR), 7.0f);
// }
// 
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFS — maximum positive displacement (0x7FFC, 4-byte aligned): no throw
// // ─────────────────────────────────────────────────────────────────────────────
// //
// 
// TEST_F(InstructionTest, STFS_MaxPositiveSI)
// {
//     cpu->m_fpr[4] = -3.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
// 
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0x7FFC;
// 
//     EXPECT_NO_THROW(Core::Instruction::STFS(*cpu, inst));
//     EXPECT_FLOAT_EQ(readFloat(cpu, TEST_ADDR + 0x7FFC), -3.0f);
// }
