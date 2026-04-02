// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_lbzx
// */
//
// #include "TestFixture.hpp"
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — basic load, zero extension
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_BasicLoad)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR + 3, 0x42);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 3;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00000042u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — 0xFF zero-extended (not sign-extended)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_0xFF_ZeroExtended)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR + 1, 0xFF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 1;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x000000FFu); // NOT 0xFFFFFFFF
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — 0x80 zero-extended (not sign-extended)
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_0x80_ZeroExtended)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x80);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00000080u); // NOT 0xFFFFFF80
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — zero byte
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_LoadZero)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x00);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — RA=0: RB provides full address
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_RA0_RBProvidesAddress)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0xAB);
//     cpu->m_gpr[0] = 0xDEAD0000; // ignored
//     cpu->m_gpr[2] = TEST_ADDR;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x000000ABu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — RB=0: EA = RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_RB0_EAEqualsRA)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x55);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00000055u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_DoesNotUpdateRA)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR + 2, 0x77);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 2;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — 0x7F zero-extended
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_0x7F_ZeroExtended)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x7F);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x0000007Fu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — 0x01 small value
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_SmallValue)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR + 5, 0x01);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 5;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x00000001u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — different RT, RA, RB registers
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_DifferentRegisters)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR + 7, 0xCC);
//     cpu->m_gpr[5] = TEST_ADDR;
//     cpu->m_gpr[6] = 7;
//
//     EncodedInstruction inst(0);
//     inst.rt = 10;
//     inst.ra = 5;
//     inst.rb = 6;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[10], 0x000000CCu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  LBZX — high bits of RT are cleared
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, LBZX_HighBitsOfRTCleared)
// {
//     cpu->m_memory.write<uint8_t>(TEST_ADDR, 0x3C);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0xFFFFFFFF; // pre-set
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::LBZX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[4], 0x0000003Cu); // high 24 bits cleared
// }
