// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stwu
// */
//
// #include "TestFixture.hpp"
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — basic: store + RA updated to EA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_BasicStoreAndUpdate)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0xDEADBEEF;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 4;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0xDEADBEEFu);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4); // RA = EA
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — RA updated to the exact effective address
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_RAUpdatedToEA)
// {
//     cpu->m_gpr[2] = TEST_ADDR;
//     cpu->m_gpr[5] = 0x12345678;
//
//     EncodedInstruction inst(0);
//     inst.rt = 5;
//     inst.ra = 2;
//     inst.si = 8;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[2], TEST_ADDR + 8);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — negative displacement: RA decremented
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_NegativeDisplacement)
// {
//     cpu->m_gpr[1] = TEST_ADDR + 16;
//     cpu->m_gpr[4] = 0xCAFEBABE;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = static_cast<uint16_t>(static_cast<int16_t>(-16));
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xCAFEBABEu);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — store 0x00000000
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_StoreZero)
// {
//     cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xFFFFFFFF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0x00000000;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 4;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0u);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — store 0xFFFFFFFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_StoreAllOnes)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0xFFFFFFFF;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 4;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0xFFFFFFFFu);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — zero displacement: RA unchanged in value, stores at RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_ZeroDisplacement)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0xABCDEF01;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xABCDEF01u);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — chained: use updated RA for next store
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_ChainedStores)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0xAAAAAAAA;
//     cpu->m_gpr[5] = 0xBBBBBBBB;
//
//     EncodedInstruction inst1(0);
//     inst1.rt = 4; inst1.ra = 1; inst1.si = 4;
//     Core::Instruction::STWU(*cpu, inst1);
//
//     // r1 = TEST_ADDR + 4
//     EncodedInstruction inst2(0);
//     inst2.rt = 5; inst2.ra = 1; inst2.si = 4;
//     Core::Instruction::STWU(*cpu, inst2);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0xAAAAAAAAu);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8), 0xBBBBBBBBu);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 8);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — 0x80000000 (high bit)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_StoreHighBit)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0x80000000;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = 4;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0x80000000u);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR + 4);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — large negative displacement (stack frame allocation pattern)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_LargeNegativeDisplacement)
// {
//     cpu->m_gpr[1] = TEST_ADDR + 0x100;
//     cpu->m_gpr[4] = 0x55667788;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.si = static_cast<uint16_t>(static_cast<int16_t>(-0x100));
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x55667788u);
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWU — different RS and RA registers
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWU_DifferentRSAndRA)
// {
//     cpu->m_gpr[3] = TEST_ADDR;
//     cpu->m_gpr[7] = 0x99AABBCC;
//
//     EncodedInstruction inst(0);
//     inst.rt = 7;
//     inst.ra = 3;
//     inst.si = 4;
//
//     Core::Instruction::STWU(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0x99AABBCCu);
//     EXPECT_EQ(cpu->m_gpr[3], TEST_ADDR + 4);
// }
