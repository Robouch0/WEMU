// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stbx
// */
//
// #include "TestFixture.hpp"
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — basic store: low byte of RS at RA+RB
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_BasicStore)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 3;
//     cpu->m_gpr[4] = 0x00000042;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 3), 0x42u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — high bits of RS are ignored
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_HighBitsIgnored)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0xABCDEF42; // only 0x42 should be stored
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x42u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — store 0xFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_Store0xFF)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 1;
//     cpu->m_gpr[4] = 0x000000FF;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 1), 0xFFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — store 0x00
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_StoreZero)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xFF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0x00000000;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — RA=0: RB provides full address
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_RA0_RBProvidesAddress)
// {
//     cpu->m_gpr[0] = 0xDEAD0000; // ignored
//     cpu->m_gpr[2] = TEST_ADDR;
//     cpu->m_gpr[4] = 0x000000AB;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 0;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0xABu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — RB=0: EA = RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_RB0_EAEqualsRA)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0x00000055;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x55u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_DoesNotUpdateRA)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 2;
//     cpu->m_gpr[4] = 0x00000077;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — store 0x80
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_Store0x80)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0x00000080;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x80u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — different RS, RA, RB registers
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_DifferentRegisters)
// {
//     cpu->m_gpr[5]  = TEST_ADDR;
//     cpu->m_gpr[6]  = 7;
//     cpu->m_gpr[10] = 0x000000CC;
//
//     EncodedInstruction inst(0);
//     inst.rs = 10;
//     inst.ra = 5;
//     inst.rb = 6;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 7), 0xCCu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — store 0x01 smallest non-zero
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_SmallestNonZero)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0x00000001;
//
//     EncodedInstruction inst(0);
//     inst.rs = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STBX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR), 0x01u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STBX — successive stores to adjacent bytes
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STBX_SuccessiveAdjacentStores)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[4] = 0x000000AA;
//     cpu->m_gpr[5] = 0x000000BB;
//
//     EncodedInstruction inst1(0);
//     inst1.rs = 4; inst1.ra = 1;
//     cpu->m_gpr[2] = 0;
//     inst1.rb = 2;
//     Core::Instruction::STBX(*cpu, inst1);
//
//     EncodedInstruction inst2(0);
//     inst2.rs = 5; inst2.ra = 1;
//     cpu->m_gpr[2] = 1;
//     inst2.rb = 2;
//     Core::Instruction::STBX(*cpu, inst2);
//
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR),     0xAAu);
//     EXPECT_EQ(cpu->m_memory.read<uint8_t>(TEST_ADDR + 1), 0xBBu);
// }
