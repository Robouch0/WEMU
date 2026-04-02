// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_stwx
// */
//
// #include "TestFixture.hpp"
//
// static constexpr uint32_t TEST_ADDR = 0x02000200;
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — basic: EA = RA + RB
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_BasicStore)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 8;
//     cpu->m_gpr[4] = 0xDEADBEEF;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8), 0xDEADBEEFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — RA=0 uses 0 as base, not r0
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_RA0_Uses0NotR0)
// {
//     cpu->m_gpr[0] = TEST_ADDR; // r0 ignored
//     cpu->m_gpr[2] = 0;         // RB=0 → EA = 0
//     cpu->m_gpr[4] = 0x12345678;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     // EA = 0 → unmapped, nothing written to TEST_ADDR
//     EXPECT_NE(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x12345678u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — RA=0: RB provides full address
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_RA0_RBProvidesAddress)
// {
//     cpu->m_gpr[0] = 0xDEAD0000; // r0 ignored
//     cpu->m_gpr[2] = TEST_ADDR;
//     cpu->m_gpr[4] = 0xCAFEBABE;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 0;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xCAFEBABEu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — RB=0: EA = RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_RB0_EAEqualsRA)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0xABCD1234;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0xABCD1234u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — store 0xFFFFFFFF
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_StoreAllOnes)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 4;
//     cpu->m_gpr[4] = 0xFFFFFFFF;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 4), 0xFFFFFFFFu);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — store 0x80000000
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_StoreHighBit)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0x80000000;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0x80000000u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — store zero
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_StoreZero)
// {
//     cpu->m_memory.write<uint32_t>(TEST_ADDR, 0xFFFFFFFF);
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0;
//     cpu->m_gpr[4] = 0x00000000;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR), 0u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — does not update RA
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_DoesNotUpdateRA)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 4;
//     cpu->m_gpr[4] = 0x11223344;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_gpr[1], TEST_ADDR);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — different RS, RA, RB registers
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_DifferentRegisters)
// {
//     cpu->m_gpr[5] = TEST_ADDR;
//     cpu->m_gpr[6] = 8;
//     cpu->m_gpr[10] = 0x55443322;
//
//     EncodedInstruction inst(0);
//     inst.rt = 10;
//     inst.ra = 5;
//     inst.rb = 6;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 8), 0x55443322u);
// }
//
// //
// // ─────────────────────────────────────────────────────────────────────────────
// //  STWX — large RB offset
// // ─────────────────────────────────────────────────────────────────────────────
// //
//
// TEST_F(InstructionTest, STWX_LargeRBOffset)
// {
//     cpu->m_gpr[1] = TEST_ADDR;
//     cpu->m_gpr[2] = 0x100;
//     cpu->m_gpr[4] = 0xBEEFCAFE;
//
//     EncodedInstruction inst(0);
//     inst.rt = 4;
//     inst.ra = 1;
//     inst.rb = 2;
//
//     Core::Instruction::STWX(*cpu, inst);
//
//     EXPECT_EQ(cpu->m_memory.read<uint32_t>(TEST_ADDR + 0x100), 0xBEEFCAFEu);
// }
