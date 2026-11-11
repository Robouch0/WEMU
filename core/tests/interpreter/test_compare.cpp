// WEMU – Compare instruction tests
//
// Covers: CMP (signed), CMPL (unsigned), CMPI (signed immediate), CMPLI
// (unsigned immediate). All use the same internal set_crf() helper.
// Tests exercise CRF selection, sign-extension of immediates, and the XER.SO
// propagation into the CR field's SO bit.

#include "TestFixture.hpp"
#include "cpu/types/EncodedInstruction.hpp"

// Helper to extract a 4-bit CR field from the raw CR register.
// PPC CR fields: cr0=bits[31:28], cr1=[27:24], …, cr7=[3:0].
static inline std::uint32_t getCRField(std::uint32_t cr_raw, std::uint32_t field_idx)
{
    // field 0 (cr0) → bits 31:28, field 7 (cr7) → bits 3:0
    return (cr_raw >> ((7u - field_idx) * 4u)) & 0xFu;
}

// CMP uses bits [25:23] of the raw word as the CRF index (via instr.rt >> 2).
static EncodedInstruction cmpInst(std::uint32_t crf, std::uint32_t ra, std::uint32_t rb)
{
    EncodedInstruction inst(0);
    inst.rt = crf << 2u; // CRF is encoded in the upper 3 bits of the RT field
    inst.ra = ra;
    inst.rb = rb;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CMP – signed compare
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InstructionTest, CMP_Equal_SetsEQ)
{
    cpu->m_gpr[3] = 42u;
    cpu->m_gpr[4] = 42u;
    EncodedInstruction inst = cmpInst(/*crf=*/0, /*ra=*/3, /*rb=*/4);
    Core::Instruction::CMP(*cpu, inst);
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 0);
    EXPECT_EQ(f & 0x2u, 0x2u); // EQ bit set
    EXPECT_EQ(f & 0x8u, 0u); // LT not set
    EXPECT_EQ(f & 0x4u, 0u); // GT not set
}

TEST_F(InstructionTest, CMP_ALessThanB_SetsLT)
{
    cpu->m_gprSigned[3] = -5;
    cpu->m_gprSigned[4] = 10;
    EncodedInstruction inst = cmpInst(0, 3, 4);
    Core::Instruction::CMP(*cpu, inst);
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 0);
    EXPECT_EQ(f & 0x8u, 0x8u); // LT
    EXPECT_EQ(f & 0x4u, 0u);
    EXPECT_EQ(f & 0x2u, 0u);
}

TEST_F(InstructionTest, CMP_AGreaterThanB_SetsGT)
{
    cpu->m_gprSigned[3] = 100;
    cpu->m_gprSigned[4] = 1;
    EncodedInstruction inst = cmpInst(0, 3, 4);
    Core::Instruction::CMP(*cpu, inst);
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 0);
    EXPECT_EQ(f & 0x4u, 0x4u); // GT
    EXPECT_EQ(f & 0x8u, 0u);
    EXPECT_EQ(f & 0x2u, 0u);
}

// CMP writes to a non-zero CRF (cr2 = field 2).
TEST_F(InstructionTest, CMP_NonZeroCRF_WritesCRF2)
{
    cpu->m_gprSigned[5] = 1;
    cpu->m_gprSigned[6] = 2;
    EncodedInstruction inst = cmpInst(/*crf=*/2, 5, 6);
    Core::Instruction::CMP(*cpu, inst);
    // CR2 field (bits [23:20]) should have LT set
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 2);
    EXPECT_EQ(f & 0x8u, 0x8u);
    // CR0 should be untouched
    EXPECT_EQ(getCRField(cpu->m_cr.raw, 0), 0u);
}

// XER.SO is propagated into the CR field's SO bit.
TEST_F(InstructionTest, CMP_XER_SO_SetsSObInCRF)
{
    cpu->m_xer.so = 1;
    cpu->m_gpr[3] = 0u;
    cpu->m_gpr[4] = 0u;
    EncodedInstruction inst = cmpInst(0, 3, 4);
    Core::Instruction::CMP(*cpu, inst);
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 0);
    EXPECT_EQ(f & 0x1u, 0x1u); // SO bit
    EXPECT_EQ(f & 0x2u, 0x2u); // EQ bit also set
}

// ─────────────────────────────────────────────────────────────────────────────
//  CMPL – unsigned compare
// ─────────────────────────────────────────────────────────────────────────────

// 0xFFFFFFFF unsigned > 1 unsigned (would be < 1 if signed).
TEST_F(InstructionTest, CMPL_UnsignedLargeVsSmall_SetsGT)
{
    cpu->m_gpr[3] = 0xFFFFFFFFu; // -1 signed, huge unsigned
    cpu->m_gpr[4] = 1u;
    EncodedInstruction inst = cmpInst(0, 3, 4);
    Core::Instruction::CMPL(*cpu, inst);
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 0);
    EXPECT_EQ(f & 0x4u, 0x4u); // GT (unsigned)
}

// 1 unsigned < 0xFFFFFFFF unsigned.
TEST_F(InstructionTest, CMPL_UnsignedSmallVsLarge_SetsLT)
{
    cpu->m_gpr[3] = 1u;
    cpu->m_gpr[4] = 0xFFFFFFFFu;
    EncodedInstruction inst = cmpInst(0, 3, 4);
    Core::Instruction::CMPL(*cpu, inst);
    const std::uint32_t f = getCRField(cpu->m_cr.raw, 0);
    EXPECT_EQ(f & 0x8u, 0x8u); // LT (unsigned)
}

// ─────────────────────────────────────────────────────────────────────────────
//  CMPI – signed compare with immediate
// ─────────────────────────────────────────────────────────────────────────────

static EncodedInstruction cmpiInst(std::uint32_t crf, std::uint32_t ra, std::int16_t si)
{
    EncodedInstruction inst(0);
    inst.rt = crf << 2u;
    inst.ra = ra;
    // si field: low 16 bits of raw
    inst = EncodedInstruction(inst.raw | (static_cast<std::uint16_t>(si)));
    return inst;
}

TEST_F(InstructionTest, CMPI_EqualToImmediate_SetsEQ)
{
    cpu->m_gprSigned[5] = 42;
    EncodedInstruction inst = cmpiInst(0, 5, 42);
    Core::Instruction::CMPI(*cpu, inst);
    EXPECT_EQ(getCRField(cpu->m_cr.raw, 0) & 0x2u, 0x2u);
}

TEST_F(InstructionTest, CMPI_NegativeImmediate_SignExtended)
{
    cpu->m_gprSigned[3] = -100;
    EncodedInstruction inst = cmpiInst(0, 3, -100);
    Core::Instruction::CMPI(*cpu, inst);
    // -100 == -100 → EQ
    EXPECT_EQ(getCRField(cpu->m_cr.raw, 0) & 0x2u, 0x2u);
}

TEST_F(InstructionTest, CMPI_ValueLessThanImmediate_SetsLT)
{
    cpu->m_gprSigned[3] = -1;
    EncodedInstruction inst = cmpiInst(0, 3, 0);
    Core::Instruction::CMPI(*cpu, inst);
    EXPECT_EQ(getCRField(cpu->m_cr.raw, 0) & 0x8u, 0x8u);
}

// ─────────────────────────────────────────────────────────────────────────────
//  CMPLI – unsigned compare with immediate
// ─────────────────────────────────────────────────────────────────────────────

static EncodedInstruction cmliInst(std::uint32_t crf, std::uint32_t ra, std::uint16_t ui)
{
    EncodedInstruction inst(0);
    inst.rt = crf << 2u;
    inst.ra = ra;
    inst = EncodedInstruction(inst.raw | ui); // zero-extended immediate
    return inst;
}

// 0xFFFFFFFF > immediate 0 unsigned → GT.
TEST_F(InstructionTest, CMPLI_LargeUnsignedGTzero_SetsGT)
{
    cpu->m_gpr[3] = 0xFFFFFFFFu;
    EncodedInstruction inst = cmliInst(0, 3, 0);
    Core::Instruction::CMPLI(*cpu, inst);
    EXPECT_EQ(getCRField(cpu->m_cr.raw, 0) & 0x4u, 0x4u);
}

// 0 < 0xFFFF unsigned → LT.
TEST_F(InstructionTest, CMPLI_ZeroLTMaxImmediate_SetsLT)
{
    cpu->m_gpr[3] = 0u;
    EncodedInstruction inst = cmliInst(0, 3, 0xFFFFu);
    Core::Instruction::CMPLI(*cpu, inst);
    EXPECT_EQ(getCRField(cpu->m_cr.raw, 0) & 0x8u, 0x8u);
}
