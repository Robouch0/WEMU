// /*
// ** EPITECH PROJECT, 2025
// ** core
// ** File description:
// ** test_rlwinm
// */

#include "TestFixture.hpp"

// RLWINM: rA = ROTL32(rS, SH) & MASK(MB, ME)
// Encoding (PPC M-form):
//   bits [6:10]  = RS
//   bits [11:15] = RA
//   bits [16:20] = SH
//   bits [21:25] = MB
//   bits [26:30] = ME
//   bit  [31]    = Rc

static uint32_t rotl32_i(uint32_t val, uint32_t sh)
{
    sh &= 31;
    return (sh == 0) ? val : ((val << sh) | (val >> (32 - sh)));
}

static uint32_t ppcMask_i(uint32_t mb, uint32_t me)
{
    if (mb <= me)
        return ((0xFFFFFFFFu >> mb) & (0xFFFFFFFFu << (31 - me)));
    return ~ppcMask_i(me + 1, mb - 1);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — basic: rotate by 8 with full mask (logical shift left by 8)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_BasicRotate)
{
    cpu->m_gpr[3] = 0xFF000000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 8;
    inst.mb = 0;
    inst.me = 31;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x000000FFu); // FF rotates from bit31:24 to bit7:0
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — SH=0: no rotation, only mask applied
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_SH0_JustMask)
{
    cpu->m_gpr[3] = 0xFFFFFFFF;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 0;
    inst.mb = 8;
    inst.me = 23;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], ppcMask_i(8, 23));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — extract high byte: SH=0, MB=0, ME=7
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_ExtractHighByte)
{
    cpu->m_gpr[3] = 0xABCDEF12;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 0;
    inst.mb = 0;
    inst.me = 7;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xAB000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — shift left 16 (SH=16, MB=0, ME=15)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_ShiftLeft16)
{
    cpu->m_gpr[3] = 0x0000ABCD;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 16;
    inst.mb = 0;
    inst.me = 15;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0xABCD0000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — SH=31: rotate right by 1 equivalent
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_SH31_RotateRight1)
{
    cpu->m_gpr[3] = 0x00000001;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 31;
    inst.mb = 0;
    inst.me = 31;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x80000000u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — shift right 16 logical (SH=16, MB=16, ME=31)
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_ShiftRight16_Logical)
{
    cpu->m_gpr[3] = 0xABCD0000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 16;
    inst.mb = 16;
    inst.me = 31;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0x0000ABCDu);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — full mask (MB=0, ME=31): result equals pure rotation
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_FullMask_PureRotation)
{
    cpu->m_gpr[3] = 0x12345678;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 12;
    inst.mb = 0;
    inst.me = 31;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], rotl32_i(0x12345678, 12));
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — RC=1 updates CR0 when result is zero
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_RC_UpdatesCR0_Zero)
{
    cpu->m_gpr[3] = 0x00000000;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 4;
    inst.mb = 0;
    inst.me = 31;
    inst.rc = 1;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[4], 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero) ? 1 : 0), 1u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative) ? 1 : 0), 0u);
    EXPECT_EQ(((cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive) ? 1 : 0), 0u);
}

//
// ─────────────────────────────────────────────────────────────────────────────
//  RLWINM — RS is not modified
// ─────────────────────────────────────────────────────────────────────────────
//

TEST_F(InstructionTest, RLWINM_DoesNotModifyRS)
{
    cpu->m_gpr[3] = 0xDEADBEEF;

    EncodedInstruction inst(0);
    inst.rs = 3;
    inst.ra = 4;
    inst.sh = 8;
    inst.mb = 0;
    inst.me = 31;

    Core::Instruction::RLWINM(*cpu, inst);

    EXPECT_EQ(cpu->m_gpr[3], 0xDEADBEEFu);
}
