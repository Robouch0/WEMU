// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lhzx
// */
//
// #include "TestFixture.hpp"
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — basic zero extension
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_BasicLoad)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x1234);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 4;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00001234u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — 0x8000 zero-extended to 0x00008000 (NOT sign-extended)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_HighBitSet_NoSignExtend)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x8000);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 4;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00008000u); // zero-extended, NOT 0xFFFF8000
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — 0xFFFF zero-extended to 0x0000FFFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_AllOnes_ZeroExtended)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0xFFFF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 4;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x0000FFFFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — zero halfword
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_LoadZero)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0000);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — 0x7FFF stays as 0x00007FFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_0x7FFF_ZeroExtended)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x7FFF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00007FFFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — RA=0: RB provides full address
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_RA0_RBProvidesAddress)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xABCD);
//     cpu->m_gpr[0] = 0xDEAD0000; // ignored
//     cpu->m_gpr[2] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x0000ABCDu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — RB=0: EA = RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_RB0_EAEqualsRA)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR, 0xBEEF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x0000BEEFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_DoesNotUpdateRA)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR + 4, 0x1111);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 4;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — different registers
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_DifferentRegisters)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR + 6, 0xCCDD);
//     cpu->m_gpr[5] = TEST_ADDR;
//     cpu->m_gpr[6] = 6;
//
//     EncodedInstruction inst(0);
//     inst.rt = 10;
//     inst.ra = 5;
//     inst.rb = 6;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[10], 0x0000CCDDu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LHZX — 0x0001 small positive value
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LHZX_SmallValue)
// {
//     cpu->m_memory.write<uint16_t>(TEST_ADDR, 0x0001);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LHZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
// }
