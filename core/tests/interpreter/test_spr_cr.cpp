// WEMU – SPR / CR move instruction tests
//
// Covers: MFSPR (XER, LR, CTR, GQR), MTSPR (XER, LR, CTR, GQR),
// MFCR, MTCRF, MCRF, and CR-logical ops (CRAND/CROR/CRXOR/CRNOR/CRNAND/
// CRANDC/CRORC/CREQV).

#include "TestFixture.hpp"
#include "cpu/types/EncodedInstruction.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR helpers
// ─────────────────────────────────────────────────────────────────────────────

// The SPR field is split-encoded: spr_field = ((spr_num >> 5) & 0x1F) | ((spr_num & 0x1F) << 5).
static constexpr std::uint32_t encodeSpr(std::uint32_t spr_num) { return ((spr_num & 0x1Fu) << 5u) | ((spr_num >> 5u) & 0x1Fu); }

static EncodedInstruction makeMfspr(std::uint32_t rt, std::uint32_t spr_num)
{
    EncodedInstruction inst(0);
    inst.rt = rt;
    // spr field is bits [20:11] in the raw word
    const std::uint32_t spr_enc = encodeSpr(spr_num);
    // Write spr field: bits [20:11], i.e. shift 11
    inst = EncodedInstruction(inst.raw | (spr_enc << 11u));
    return inst;
}

static EncodedInstruction makeMtspr(std::uint32_t rs, std::uint32_t spr_num)
{
    // rs is in the rt position (bits [25:21])
    EncodedInstruction inst(0);
    inst.rt = rs;
    const std::uint32_t spr_enc = encodeSpr(spr_num);
    inst = EncodedInstruction(inst.raw | (spr_enc << 11u));
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
//  MFSPR
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InstructionTest, MFSPR_XER_ReadsXER)
{
    cpu->m_xer.raw = 0x60000000u; // SO=1, OV=1
    EncodedInstruction inst = makeMfspr(/*rt=*/5, /*spr=*/1);
    Core::Instruction::MFSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[5], 0x60000000u);
}

TEST_F(InstructionTest, MFSPR_LR_ReadsLR)
{
    cpu->m_lr = 0xDEADBEEFu;
    EncodedInstruction inst = makeMfspr(/*rt=*/7, /*spr=*/8);
    Core::Instruction::MFSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[7], 0xDEADBEEFu);
}

TEST_F(InstructionTest, MFSPR_CTR_ReadsCTR)
{
    cpu->m_ctr = 0x12345678u;
    EncodedInstruction inst = makeMfspr(/*rt=*/10, /*spr=*/9);
    Core::Instruction::MFSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[10], 0x12345678u);
}

// GQR0 (SPR 896 or 912 – both map to m_gqr[0])
TEST_F(InstructionTest, MFSPR_GQR0_ReadsGQR)
{
    cpu->m_gqr[0] = 0xAABBCCDDu;
    EncodedInstruction inst = makeMfspr(/*rt=*/4, /*spr=*/912);
    Core::Instruction::MFSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[4], 0xAABBCCDDu);
}

TEST_F(InstructionTest, MFSPR_GQR3_ReadsGQR)
{
    cpu->m_gqr[3] = 0x11223344u;
    EncodedInstruction inst = makeMfspr(/*rt=*/6, /*spr=*/915);
    Core::Instruction::MFSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[6], 0x11223344u);
}

// Unknown SPR leaves destination register unchanged (silent skip).
TEST_F(InstructionTest, MFSPR_UnknownSPR_RegisterUnchanged)
{
    cpu->m_gpr[5] = 0x99999999u;
    EncodedInstruction inst = makeMfspr(/*rt=*/5, /*spr=*/500);
    Core::Instruction::MFSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[5], 0x99999999u);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MTSPR
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InstructionTest, MTSPR_LR_WritesLR)
{
    cpu->m_gpr[5] = 0xCAFEBABEu;
    EncodedInstruction inst = makeMtspr(/*rs=*/5, /*spr=*/8);
    // rs is in instr.rs; construct properly
    EncodedInstruction inst2(0);
    inst2.rs = 5;
    const std::uint32_t spr_enc = encodeSpr(8);
    inst2 = EncodedInstruction(inst2.raw | (spr_enc << 11u));
    Core::Instruction::MTSPR(*cpu, inst2);
    EXPECT_EQ(cpu->m_lr, 0xCAFEBABEu);
}

TEST_F(InstructionTest, MTSPR_CTR_WritesCTR)
{
    cpu->m_gpr[3] = 0xDEAD1234u;
    EncodedInstruction inst(0);
    inst.rs = 3;
    const std::uint32_t spr_enc = encodeSpr(9);
    inst = EncodedInstruction(inst.raw | (spr_enc << 11u));
    Core::Instruction::MTSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_ctr, 0xDEAD1234u);
}

TEST_F(InstructionTest, MTSPR_XER_WritesXER)
{
    cpu->m_gpr[8] = 0x40000000u; // OV=1
    EncodedInstruction inst(0);
    inst.rs = 8;
    const std::uint32_t spr_enc = encodeSpr(1);
    inst = EncodedInstruction(inst.raw | (spr_enc << 11u));
    Core::Instruction::MTSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_xer.raw, 0x40000000u);
}

TEST_F(InstructionTest, MTSPR_GQR1_WritesGQR)
{
    cpu->m_gpr[9] = 0x55AA55AAu;
    EncodedInstruction inst(0);
    inst.rs = 9;
    const std::uint32_t spr_enc = encodeSpr(913); // GQR1
    inst = EncodedInstruction(inst.raw | (spr_enc << 11u));
    Core::Instruction::MTSPR(*cpu, inst);
    EXPECT_EQ(cpu->m_gqr[1], 0x55AA55AAu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MFCR / MTCRF
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InstructionTest, MFCR_ReadsCRIntoGPR)
{
    cpu->m_cr.raw = 0xA5A5A5A5u;
    EncodedInstruction inst(0);
    inst.rt = 7;
    Core::Instruction::MFCR(*cpu, inst);
    EXPECT_EQ(cpu->m_gpr[7], 0xA5A5A5A5u);
}

// MTCRF with FXM=0xFF: all CR fields replaced from rs.
TEST_F(InstructionTest, MTCRF_AllFields_ReplacesCR)
{
    cpu->m_gpr[4] = 0x12345678u;
    cpu->m_cr.raw = 0xFFFFFFFFu;
    EncodedInstruction inst(0);
    inst.rs = 4;
    // FXM is bits [19:12]; set all 8 bits
    inst = EncodedInstruction(inst.raw | (0xFFu << 12u));
    Core::Instruction::MTCRF(*cpu, inst);
    EXPECT_EQ(cpu->m_cr.raw, 0x12345678u);
}

// MTCRF with FXM=0x01 (bit 0 → CR7): only CR7 (the low 4 bits) replaced.
TEST_F(InstructionTest, MTCRF_OnlyCR7_OtherFieldsPreserved)
{
    cpu->m_gpr[4] = 0x0000000Fu; // CR7 would be 0xF if fully written
    cpu->m_cr.raw = 0xABCDEFABu; // CR7 is bits [3:0] = 0xB
    EncodedInstruction inst(0);
    inst.rs = 4;
    // FXM bit 0 selects CR7 (the LSB field)
    inst = EncodedInstruction(inst.raw | (0x01u << 12u));
    Core::Instruction::MTCRF(*cpu, inst);
    // Only the low 4 bits (CR7) should change to 0xF; rest stays 0xABCDEFA?
    EXPECT_EQ(cpu->m_cr.raw & 0xFFFFFFF0u, 0xABCDEFA0u);
    EXPECT_EQ(cpu->m_cr.raw & 0x0Fu, 0x0Fu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MCRF
// ─────────────────────────────────────────────────────────────────────────────

// MCRF copies CR field BFA → BF.  BF=0 (CR0), BFA=1 (CR1).
TEST_F(InstructionTest, MCRF_CopiesCRFieldBFAtoBF)
{
    // CR1 (bits [27:24]) = 0xC = 0b1100
    cpu->m_cr.raw = 0x0C000000u; // CR1=0xC, rest=0
    // BF=0 (CR0) = bits [31:28], BFA=1 (CR1) = bits [27:24]
    // Raw encoding: BF at bits [25:23], BFA at bits [20:18]
    EncodedInstruction inst(0);
    inst = EncodedInstruction((0u << 23u) | (1u << 18u)); // BF=0, BFA=1
    Core::Instruction::MCRF(*cpu, inst);
    // CR0 should now equal CR1 = 0xC
    EXPECT_EQ(cpu->m_cr.cr0, 0xCu);
    // CR1 unchanged
    EXPECT_EQ(cpu->m_cr.cr1, 0xCu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  CR logical operations
// ─────────────────────────────────────────────────────────────────────────────

static EncodedInstruction crLogInst(std::uint32_t bt, std::uint32_t ba, std::uint32_t bb)
{
    // BT = rt (bits [25:21]), BA = ra (bits [20:16]), BB = rb (bits [15:11])
    EncodedInstruction inst(0);
    inst.rt = bt;
    inst.ra = ba;
    inst.rb = bb;
    return inst;
}

// CRAND: 1 AND 1 = 1
TEST_F(InstructionTest, CRAND_BothOne_ResultOne)
{
    // Set CR bits 0 and 1 (PPC bit 0 = raw bit 31, bit 1 = raw bit 30)
    cpu->m_cr.raw = (1u << 31u) | (1u << 30u);
    EncodedInstruction inst = crLogInst(/*bt=*/2, /*ba=*/0, /*bb=*/1);
    Core::Instruction::CRAND(*cpu, inst);
    // Result in CR bit 2 (raw bit 29)
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CRAND: 1 AND 0 = 0
TEST_F(InstructionTest, CRAND_OneAndZero_ResultZero)
{
    cpu->m_cr.raw = (1u << 31u); // bit 0 = 1, bit 1 = 0
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRAND(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 0u);
}

// CROR: 0 OR 1 = 1
TEST_F(InstructionTest, CROR_ZeroOrOne_ResultOne)
{
    cpu->m_cr.raw = (1u << 30u); // bit 1 set
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CROR(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CRXOR: 1 XOR 1 = 0
TEST_F(InstructionTest, CRXOR_BothOne_ResultZero)
{
    cpu->m_cr.raw = (1u << 31u) | (1u << 30u);
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRXOR(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 0u);
}

// CRXOR: 1 XOR 0 = 1
TEST_F(InstructionTest, CRXOR_OneXorZero_ResultOne)
{
    cpu->m_cr.raw = (1u << 31u); // bit 0 = 1, bit 1 = 0
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRXOR(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CRNAND: 1 NAND 1 = 0
TEST_F(InstructionTest, CRNAND_BothOne_ResultZero)
{
    cpu->m_cr.raw = (1u << 31u) | (1u << 30u);
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRNAND(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 0u);
}

// CRNOR: 0 NOR 0 = 1
TEST_F(InstructionTest, CRNOR_BothZero_ResultOne)
{
    cpu->m_cr.raw = 0;
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRNOR(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CRANDC: 1 AND NOT(1) = 0
TEST_F(InstructionTest, CRANDC_OneAndNotOne_ResultZero)
{
    cpu->m_cr.raw = (1u << 31u) | (1u << 30u);
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRANDC(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 0u);
}

// CRANDC: 1 AND NOT(0) = 1
TEST_F(InstructionTest, CRANDC_OneAndNotZero_ResultOne)
{
    cpu->m_cr.raw = (1u << 31u); // bit 0 = 1, bit 1 = 0
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRANDC(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CRORC: 0 OR NOT(0) = 1
TEST_F(InstructionTest, CRORC_ZeroOrNotZero_ResultOne)
{
    cpu->m_cr.raw = 0; // both zero
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CRORC(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CREQV: 1 EQUIV 1 = 1
TEST_F(InstructionTest, CREQV_BothOne_ResultOne)
{
    cpu->m_cr.raw = (1u << 31u) | (1u << 30u);
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CREQV(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 1u);
}

// CREQV: 1 EQUIV 0 = 0
TEST_F(InstructionTest, CREQV_OneEquivZero_ResultZero)
{
    cpu->m_cr.raw = (1u << 31u); // bit 0=1, bit 1=0
    EncodedInstruction inst = crLogInst(2, 0, 1);
    Core::Instruction::CREQV(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> 29u) & 1u, 0u);
}

// CRXOR on the same bit: self-XOR always gives 0 (convenient idiom in PPC to clear a CR bit).
TEST_F(InstructionTest, CRXOR_SameBit_ClearsBit)
{
    // Set bit 5
    cpu->m_cr.raw = (1u << (31u - 5u));
    EncodedInstruction inst = crLogInst(/*bt=*/5, /*ba=*/5, /*bb=*/5);
    Core::Instruction::CRXOR(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> (31u - 5u)) & 1u, 0u);
}

// CROR on the same bit: self-OR is a NOP (bit unchanged).
TEST_F(InstructionTest, CROR_SameBit_NOP)
{
    // Set bit 3
    cpu->m_cr.raw = (1u << (31u - 3u));
    EncodedInstruction inst = crLogInst(3, 3, 3);
    Core::Instruction::CROR(*cpu, inst);
    EXPECT_EQ((cpu->m_cr.raw >> (31u - 3u)) & 1u, 1u);
}
