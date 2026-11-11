// WEMU – Branch / indirect-branch instruction tests
//
// Covers: B (relative, absolute, link variants), BC (conditional: CTR,
// CR, combined), BCLR (branch-to-LR), BCTR (branch-to-CTR), BL/BLA/BA,
// LK-bit LR write, AA-bit absolute vs relative target, PC/nextPc updates.

#include "TestFixture.hpp"
#include "cpu/types/EncodedInstruction.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  B – unconditional branch
// ─────────────────────────────────────────────────────────────────────────────

// B rel: m_nextPc = m_pc + offset.  LI field encodes offset >> 2.
TEST_F(InstructionTest, B_Relative_SetsNextPc)
{
    cpu->m_pc = 0x100;
    cpu->m_nextPc = 0x104; // normally set by step()

    // Build B with li=10 (offset = 10<<2 = 40 bytes forward)
    // B: opcd=18, AA=0, LK=0
    // li is bits[25:2] (24 bits), raw offset = li<<2, sign-extended to 26 bits
    // For offset +40: li = 10 (10<<2 = 40)
    std::uint32_t w = (18u << 26) | (10u << 2) | (0u << 1) | 0u; // LK=0, AA=0
    EncodedInstruction inst(w);
    Core::Instruction::B(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, 0x100u + 40u);
}

// B rel negative: branch backwards.
TEST_F(InstructionTest, B_Relative_NegativeOffset)
{
    cpu->m_pc = 0x200;
    // li = -4 as 24-bit two's complement = 0xFFFFFC
    const std::int32_t li_val = -4; // means offset = -4<<2 = -16
    const std::uint32_t li_bits = (static_cast<std::uint32_t>(li_val) & 0x00FFFFFF);
    std::uint32_t w = (18u << 26) | (li_bits << 2);
    EncodedInstruction inst(w);
    Core::Instruction::B(*cpu, inst);
    // 0x200 + (-16) = 0x1F0
    EXPECT_EQ(cpu->m_nextPc, static_cast<std::uint32_t>(0x200 + (-16)));
}

// BL: sets LR = m_pc + 4 and sets nextPc.
TEST_F(InstructionTest, BL_SetsLR_AndNextPc)
{
    cpu->m_pc = 0x1000;
    cpu->m_lr = 0;
    // li=5 → offset=20
    std::uint32_t w = (18u << 26) | (5u << 2) | 0u; // LK=1 is bit 0 of raw for B
    // Actually: for B instruction, lk=1 means bit 0 set.  But BL is a separate
    // instruction with lk=1 baked in.  We call BL directly.
    EncodedInstruction inst(w);
    // Set lk bit in raw
    inst.lk = 1;
    Core::Instruction::B(*cpu, inst); // B with lk=1 behaves as BL
    EXPECT_EQ(cpu->m_lr, 0x1004u);
    EXPECT_EQ(cpu->m_nextPc, 0x1000u + 20u);
}

// BA: absolute address (offset - ApplicationCode).
TEST_F(InstructionTest, BA_Absolute_SetsNextPcRelativeToBase)
{
    cpu->m_pc = 0x500;
    // li = 0x200 >> 2 = 128 → offset = 0x200 (absolute PPC address 0x02000200)
    // m_nextPc = offset - ApplicationCode = 0x200
    const std::uint32_t offset_bytes = 0x200;
    const std::uint32_t li_val = offset_bytes >> 2;
    std::uint32_t w = (18u << 26) | (li_val << 2) | (1u << 1); // AA=1
    EncodedInstruction inst(w);
    Core::Instruction::BA(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, offset_bytes - Core::Memory::MemoryMap::ApplicationCode);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BC – conditional branch
// ─────────────────────────────────────────────────────────────────────────────

// BO=20 (0b10100): always branch (bit2=1 → no CTR, bit4=1 → no CR).
TEST_F(InstructionTest, BC_Always_BO20_Taken)
{
    cpu->m_pc = 0x100;
    // BD field = 20 bytes forward → bd = 20 as a 16-bit signed, stored in raw[15:2]
    // BD raw bits [15:2] encode the signed offset; offset = bd << 2 (already included in raw)
    // For +20 bytes: raw[15:0] = 20 (since bd is in bits [15:2] and already a byte offset)
    // The BC instruction reads: bd = sign_extend(raw & 0xFFFC) as int16_t
    // So raw & 0xFFFC = 20 (in bytes, already)
    const std::uint16_t bd_raw = 20; // 20 bytes forward
    std::uint32_t w = (16u << 26) // opcd=16 for BC
                      | (20u << 21) // BO=20
                      | (0u << 16) // BI=0
                      | (bd_raw & 0xFFFC); // BD field
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, 0x100u + 20u);
}

// BO=4 (0b00100): branch if CR[BI]==0 (no CTR check).  When CR bit is 0, branch taken.
TEST_F(InstructionTest, BC_BNE_CRBitZero_Taken)
{
    cpu->m_pc = 0x100;
    cpu->m_cr.raw = 0; // CR0.EQ = 0
    const std::uint32_t bo = 4u; // ignore CTR, test CR, branch if CR=0
    const std::uint32_t bi = 2u; // CR bit 2 (CR0.EQ)
    const std::uint16_t bd_raw = 8;
    std::uint32_t w = (16u << 26) | (bo << 21) | (bi << 16) | (bd_raw & 0xFFFC);
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, 0x100u + 8u);
}

// Same BO=4 but CR[BI] is 1: branch NOT taken (nextPc unchanged from initial).
TEST_F(InstructionTest, BC_BNE_CRBitOne_NotTaken)
{
    cpu->m_pc = 0x200;
    cpu->m_nextPc = 0x204;
    // Set CR bit 2 (CR0.EQ) = 1 → PPC bit 29 from MSB = raw bit (31-2) = bit 29
    cpu->m_cr.raw = (1u << (31u - 2u));
    const std::uint32_t bo = 4u;
    const std::uint32_t bi = 2u;
    const std::uint16_t bd_raw = 16;
    std::uint32_t w = (16u << 26) | (bo << 21) | (bi << 16) | (bd_raw & 0xFFFC);
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, 0x204u); // not taken – nextPc unchanged
}

// CTR decrement: BO=0 (0b00000) → decrement CTR, branch if CTR!=0 AND CR[BI]=0.
TEST_F(InstructionTest, BC_DecrCTR_CTRNonZero_CRZero_Taken)
{
    cpu->m_pc = 0x100;
    cpu->m_ctr = 3u;
    cpu->m_cr.raw = 0;
    // BO bits: bit2=0 (use CTR), bit1=0 (branch if CTR!=0), bit4=0 (test CR), bit3=0 (branch if CR=0)
    const std::uint32_t bo = 0u;
    const std::uint32_t bi = 0u;
    const std::uint16_t bd_raw = 4;
    std::uint32_t w = (16u << 26) | (bo << 21) | (bi << 16) | (bd_raw & 0xFFFC);
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_ctr, 2u); // decremented
    EXPECT_EQ(cpu->m_nextPc, 0x100u + 4u); // taken
}

// CTR decrement: branch-if-CTR==0 (BO bit1=1). CTR starts at 1 → after decrement = 0 → taken.
TEST_F(InstructionTest, BC_DecrCTR_CTRBecomesZero_Taken)
{
    cpu->m_pc = 0x100;
    cpu->m_ctr = 1u;
    cpu->m_cr.raw = 0;
    // BO = 0b10010 (bdz): ignore CR, decrement CTR, branch if CTR==0
    const std::uint32_t bo = 18u;
    const std::uint16_t bd_raw = 8;
    std::uint32_t w = (16u << 26) | (bo << 21) | (0u << 16) | (bd_raw & 0xFFFC);
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_ctr, 0u);
    EXPECT_EQ(cpu->m_nextPc, 0x100u + 8u);
}

// CTR decrement: branch-if-CTR==0, but CTR starts at 2 → becomes 1 ≠ 0 → not taken.
TEST_F(InstructionTest, BC_DecrCTR_CTRNonZeroAfterDecr_NotTaken)
{
    cpu->m_pc = 0x100;
    cpu->m_nextPc = 0x104;
    cpu->m_ctr = 2u;
    cpu->m_cr.raw = 0;
    const std::uint32_t bo = 18u; // bdz: branch if CTR==0 after decrement; ignore CR
    const std::uint16_t bd_raw = 8;
    std::uint32_t w = (16u << 26) | (bo << 21) | (0u << 16) | (bd_raw & 0xFFFC);
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_ctr, 1u);
    EXPECT_EQ(cpu->m_nextPc, 0x104u); // not taken
}

// BC with LK=1: LR = m_pc + 4 when branch taken.
TEST_F(InstructionTest, BC_WithLK_SetsLROnTaken)
{
    cpu->m_pc = 0x300;
    cpu->m_lr = 0;
    const std::uint16_t bd_raw = 12;
    // BO=20 (always), lk=1
    std::uint32_t w = (16u << 26) | (20u << 21) | (0u << 16) | (bd_raw & 0xFFFC) | 1u; // LK=1
    EncodedInstruction inst(w);
    Core::Instruction::BC(*cpu, inst);
    EXPECT_EQ(cpu->m_lr, 0x304u);
    EXPECT_EQ(cpu->m_nextPc, 0x300u + 12u);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BCLR – branch-to-LR (BLR alias: BO=20)
// ─────────────────────────────────────────────────────────────────────────────

// BLR (BO=20, no CTR, no CR): nextPc = LR.
TEST_F(InstructionTest, BCLR_AlwaysTaken_SetsNextPcToLR)
{
    cpu->m_lr = 0x2000u;
    cpu->m_pc = 0x1000u;
    // BCLR raw: opcd=19, XO10=16, BO=20, BI=0, LK=0
    // Build via struct fields
    EncodedInstruction inst(0);
    inst.rt = 20u; // BO
    inst.ra = 0u; // BI
    inst.lk = 0;
    Core::Instruction::BCLR(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, 0x2000u);
}

// BCLR with LK=1 (BLRL): saves return address in LR before branching.
TEST_F(InstructionTest, BCLR_WithLK_SavesReturnAddr)
{
    cpu->m_pc = 0x500u;
    cpu->m_lr = 0x3000u;
    EncodedInstruction inst(0);
    inst.rt = 20u; // BO=20 always
    inst.lk = 1;
    Core::Instruction::BCLR(*cpu, inst);
    EXPECT_EQ(cpu->m_lr, 0x504u); // saved return
    EXPECT_EQ(cpu->m_nextPc, 0x3000u); // jumped to old LR
}

// BCLR with BO=4 (CR-dependent), CR bit set → not taken, LR restored.
TEST_F(InstructionTest, BCLR_NotTaken_LRRestored_WithLK)
{
    cpu->m_pc = 0x700u;
    cpu->m_lr = 0xABCDu;
    // BO=4: test CR, branch if CR[BI]=0.  Set CR[BI=2]=1 → not taken.
    cpu->m_cr.raw = (1u << (31u - 2u));
    EncodedInstruction inst(0);
    inst.rt = 4u; // BO=4
    inst.ra = 2u; // BI=2
    inst.lk = 1;
    cpu->m_nextPc = 0x704u;
    Core::Instruction::BCLR(*cpu, inst);
    EXPECT_EQ(cpu->m_lr, 0xABCDu); // restored
    EXPECT_EQ(cpu->m_nextPc, 0x704u); // not taken
}

// ─────────────────────────────────────────────────────────────────────────────
//  BCTR – branch-to-CTR
// ─────────────────────────────────────────────────────────────────────────────

// BCTR with BO=20 (always): nextPc = CTR - ApplicationCode.
TEST_F(InstructionTest, BCTR_Always_SetsNextPcFromCTR)
{
    // CTR must hold an absolute PPC address in the application-code range.
    const std::uint32_t appcode = Core::Memory::MemoryMap::ApplicationCode;
    cpu->m_ctr = appcode + 0x200u; // absolute PPC address
    cpu->m_pc = 0x100u;
    EncodedInstruction inst(0);
    inst.rt = 20u; // BO=20 always
    inst.lk = 0;
    Core::Instruction::BCTR(*cpu, inst);
    EXPECT_EQ(cpu->m_nextPc, 0x200u);
}

// BCTR with LK=1: saves LR = m_pc + 4.
TEST_F(InstructionTest, BCTR_WithLK_SavesLR)
{
    const std::uint32_t appcode = Core::Memory::MemoryMap::ApplicationCode;
    cpu->m_ctr = appcode + 0x400u;
    cpu->m_pc = 0x200u;
    cpu->m_lr = 0;
    EncodedInstruction inst(0);
    inst.rt = 20u;
    inst.lk = 1;
    Core::Instruction::BCTR(*cpu, inst);
    EXPECT_EQ(cpu->m_lr, 0x204u);
    EXPECT_EQ(cpu->m_nextPc, 0x400u);
}

// BCTR to a sentinel address (≥ 0xC0000000) with no registered handler:
// r3 is set to 0 and nextPc = LR.
TEST_F(InstructionTest, BCTR_SentinelNoHandler_R3Zero_ReturnToLR)
{
    // Use a sentinel address not registered as an import
    cpu->m_ctr = 0xC0FFFF00u;
    cpu->m_pc = 0x100u;
    cpu->m_lr = 0x500u;
    cpu->m_gpr[3] = 0xDEADBEEFu;
    EncodedInstruction inst(0);
    inst.rt = 20u; // BO=20 always
    inst.lk = 0;
    Core::Instruction::BCTR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[3], 0u);
    EXPECT_EQ(cpu->m_nextPc, 0x500u);
}
