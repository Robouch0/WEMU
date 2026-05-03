// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stfd
// */
//
// #include "TestFixture.hpp"
// #include <cstring>
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// // STFD: stores FRS as 64-bit double to EA.
// // EA = (RA|0) + EXTS(D). RA=0 means base is 0.
// // Note: m_fpr is float[], so STFD promotes float to double before storing.
// // Fields: inst.rt=FRS, inst.ra=RA, inst.si=D
//
// static float readDoubleAsFloat(Core::Interpreter *cpu, uint32_t addr)
// {
//     // Read the 8-byte double and convert back to float for comparison
//     uint32_t hi = cpu->m_memory.read<uint32_t>(addr);
//     uint32_t lo = cpu->m_memory.read<uint32_t>(addr + 4);
//     uint64_t bits = (static_cast<uint64_t>(hi) << 32) | lo;
//     double d;
//     std::memcpy(&d, &bits, sizeof(d));
//     return static_cast<float>(d);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — basic store of 1.0
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_BasicStore_One)
// {
//     cpu->m_fpr[4] = 1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR), 1.0f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — store -1.0
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_StoreNegativeOne)
// {
//     cpu->m_fpr[4] = -1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR), -1.0f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — store 0.0
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_StoreZero)
// {
//     cpu->m_fpr[4] = 0.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR), 0.0f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — positive displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_PositiveDisplacement)
// {
//     cpu->m_fpr[4] = 2.5f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 8;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR + 8), 2.5f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — RA=0 uses 0 as base → exception
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_RA0_Uses0NotR0)
// {
//     cpu->m_fpr[4] = 1.0f;
//     cpu->m_gpr[0] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.si = 0;
//
//     EXPECT_THROW(Core::Instruction::STFD(*cpu, inst), Core::MemoryException);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_DoesNotUpdateRA)
// {
//     cpu->m_fpr[4] = 1.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — FPR source is not modified
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_DoesNotModifyFPR)
// {
//     cpu->m_fpr[4] = -2.5f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(cpu->m_fpr[4], -2.5f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — FRS=f0 is allowed as source
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_FRS0_Allowed)
// {
//     cpu->m_fpr[0] = 5.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 0;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR), 5.0f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — maximum positive displacement (0x7FF8, 8-byte aligned): no throw
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_MaxPositiveSI)
// {
//     cpu->m_fpr[4] = 2.0f;
//     cpu->m_gpr[1] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0x7FF8;
//
//     EXPECT_NO_THROW(Core::Instruction::STFD(*cpu, inst));
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR + 0x7FF8), 2.0f);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STFD — negative displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STFD_NegativeDisplacement)
// {
//     cpu->m_fpr[4] = -4.0f;
//     cpu->m_gpr[1] = TEST_ADDR + 16;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));
//
//     Core::Instruction::STFD(*cpu, inst);
//
//     EXPECT_FLOAT_EQ(readDoubleAsFloat(cpu, TEST_ADDR), -4.0f);
// }
