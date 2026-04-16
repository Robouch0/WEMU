// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stmw
// */
//
// #include "TestFixture.hpp"
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — store single register (RS=31)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_SingleRegister)
// {
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[31] = 0xDEADBEEF;
//
//     EncodedInstruction inst(0);
//     inst.rs = 31;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xDEADBEEFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — store 2 registers (RS=30): r30, r31
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_TwoRegisters)
// {
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[30] = 0x11111111;
//     cpu->m_gpr[31] = 0x22222222;
//
//     EncodedInstruction inst(0);
//     inst.rs = 30;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),     0x11111111u);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0x22222222u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — store 4 registers (RS=28): r28..r31
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_FourRegisters)
// {
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[28] = 0xAAAAAAAA;
//     cpu->m_gpr[29] = 0xBBBBBBBB;
//     cpu->m_gpr[30] = 0xCCCCCCCC;
//     cpu->m_gpr[31] = 0xDDDDDDDD;
//
//     EncodedInstruction inst(0);
//     inst.rs = 28;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),      0xAAAAAAAAu);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4),  0xBBBBBBBBu);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8),  0xCCCCCCCCu);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 12), 0xDDDDDDDDu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — registers stored in sequential order (RS, RS+1, ..., r31)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_SequentialOrder)
// {
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[28] = 0x10000000;
//     cpu->m_gpr[29] = 0x20000000;
//     cpu->m_gpr[30] = 0x30000000;
//     cpu->m_gpr[31] = 0x40000000;
//
//     EncodedInstruction inst(0);
//     inst.rs = 28;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),      0x10000000u);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4),  0x20000000u);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8),  0x30000000u);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 12), 0x40000000u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — positive displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_PositiveDisplacement)
// {
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[30] = 0xFEEDFACE;
//     cpu->m_gpr[31] = 0xC0FFEE00;
//
//     EncodedInstruction inst(0);
//     inst.rs = 30;
//     inst.ra = 1;
//     inst.si = 8;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8),  0xFEEDFACEu);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 12), 0xC0FFEE00u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — negative displacement
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_NegativeDisplacement)
// {
//     cpu->m_gpr[1]  = TEST_ADDR + 8;
//     cpu->m_gpr[30] = 0x12345678;
//     cpu->m_gpr[31] = 0x9ABCDEF0;
//
//     EncodedInstruction inst(0);
//     inst.rs = 30;
//     inst.ra = 1;
//     inst.si = static_cast<uint16_t>(static_cast<int16_t>(-8));
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),     0x12345678u);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0x9ABCDEF0u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — RA=0 uses 0 as base
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_RA0_Uses0AsBase)
// {
//     cpu->m_gpr[0]  = TEST_ADDR; // r0 ignored
//     cpu->m_gpr[31] = 0x55555555;
//
//     EncodedInstruction inst(0);
//     inst.rs = 31;
//     inst.ra = 0;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     // EA = 0 → unmapped, TEST_ADDR untouched
//     EXPECT_NE(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x55555555u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — store zeros
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_StoreZeros)
// {
//     cpu->m_memory.write<uint32_t>(TEST_ADDR,     0xFFFFFFFF);
//     cpu->m_memory.write<uint32_t>(TEST_ADDR + 4, 0xFFFFFFFF);
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[30] = 0;
//     cpu->m_gpr[31] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rs = 30;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),     0u);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — 3 registers (RS=29)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_ThreeRegisters)
// {
//     cpu->m_gpr[1]  = TEST_ADDR;
//     cpu->m_gpr[29] = 0x0A0B0C0D;
//     cpu->m_gpr[30] = 0x1A1B1C1D;
//     cpu->m_gpr[31] = 0x2A2B2C2D;
//
//     EncodedInstruction inst(0);
//     inst.rs = 29;
//     inst.ra = 1;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR),     0x0A0B0C0Du);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0x1A1B1C1Du);
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8), 0x2A2B2C2Du);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STMW — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STMW_DoesNotUpdateRA)
// {
//     cpu->m_gpr[2]  = TEST_ADDR;
//     cpu->m_gpr[31] = 0x77777777;
//
//     EncodedInstruction inst(0);
//     inst.rs = 31;
//     inst.ra = 2;
//     inst.si = 0;
//
//     Core::Instruction::STMW(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[2], TEST_ADDR);
// }
