// WEMU – Interpreter dispatch tests
//
// Covers: findInstructionID (primary + extended opcode lookup, XO9/XO10/OE/Rc
// variants, unknown instruction), executeInstruction decode-cache path,
// updateCR0 / updateCR1 / updateOverflow helpers through direct calls,
// and the syscall-cache invalidation path (syscallFor).
//
// Tests are written against the internal behaviour of the interpreter; they do
// NOT start the run() loop and do NOT require a loaded binary.

#include "TestFixture.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build a raw ADD word (opcd=31, xo9=266, oe=0, rc=0, rt=1, ra=2, rb=3).
static constexpr std::uint32_t makeAddWord(std::uint32_t rt, std::uint32_t ra, std::uint32_t rb, std::uint32_t oe = 0, std::uint32_t rc = 0)
{
    // ADD: opcd(31) xo9(266) form:
    // bits 31-26: opcd (6)
    // bits 25-21: rt   (5)
    // bits 20-16: ra   (5)
    // bits 15-11: rb   (5)
    // bit  10:    oe
    // bits  9-1:  xo9 (266 = 0b100001010)
    // bit   0:    rc
    std::uint32_t w = 0;
    w |= (31u << 26); // opcd
    w |= (rt << 21);
    w |= (ra << 16);
    w |= (rb << 11);
    w |= (oe << 10);
    w |= (266u << 1); // xo9
    w |= rc;
    return w;
}

// Build an ADDI word (opcd=14, rt, ra, si).
static constexpr std::uint32_t makeAddiWord(std::uint32_t rt, std::uint32_t ra, std::int16_t si)
{
    std::uint32_t w = 0;
    w |= (14u << 26); // opcd
    w |= (rt << 21);
    w |= (ra << 16);
    w |= static_cast<std::uint32_t>(static_cast<std::uint16_t>(si)); // low 16 bits
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
//  findInstructionID – primary opcode dispatch
// ─────────────────────────────────────────────────────────────────────────────

// ADDI lives at primary opcode 14 with no extended opcode.  findInstructionID
// must return E_ADDI.
TEST_F(InstructionTest, FindID_PrimaryOpcodeOnly_ADDI)
{
    EncodedInstruction inst(makeAddiWord(5, 3, 10));
    const InstructionID id = cpu->findInstructionID(inst);
    EXPECT_EQ(id, InstructionID::E_ADDI);
}

// ADD lives at opcd=31, xo9=266.  findInstructionID must distinguish it from
// the dozens of other opcd-31 instructions.
TEST_F(InstructionTest, FindID_ExtendedXO9_ADD)
{
    EncodedInstruction inst(makeAddWord(1, 2, 3));
    const InstructionID id = cpu->findInstructionID(inst);
    EXPECT_EQ(id, InstructionID::E_ADD);
}

// ADDO is ADD with OE=1.  The xo9 field is the same (266); OE is ignored by
// the dispatcher – both should decode to E_ADD.
TEST_F(InstructionTest, FindID_OEBitIgnoredByDispatcher)
{
    EncodedInstruction with_oe(makeAddWord(1, 2, 3, /*oe=*/1));
    const InstructionID id = cpu->findInstructionID(with_oe);
    EXPECT_EQ(id, InstructionID::E_ADD);
}

// ADDE sits at opcd=31, xo9=138 – verify the right ID comes back.
TEST_F(InstructionTest, FindID_ExtendedXO9_ADDE)
{
    // Build an ADDE word: opcd=31, rt=4, ra=5, rb=6, oe=0, xo9=138, rc=0
    std::uint32_t w = (31u << 26) | (4u << 21) | (5u << 16) | (6u << 11) | (138u << 1);
    EncodedInstruction inst(w);
    EXPECT_EQ(cpu->findInstructionID(inst), InstructionID::E_ADDE);
}

// MULLW sits at opcd=31, xo9=235.
TEST_F(InstructionTest, FindID_ExtendedXO9_MULLW)
{
    std::uint32_t w = (31u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (235u << 1);
    EncodedInstruction inst(w);
    EXPECT_EQ(cpu->findInstructionID(inst), InstructionID::E_MULLW);
}

// OR lives at opcd=31, xo10=444 – verify XO10-based dispatch.
TEST_F(InstructionTest, FindID_ExtendedXO10_OR)
{
    // opcd=31, rs=4, ra=5, rb=6, rc=0, xo10=444
    // xo10 is bits [10:1] in the raw word
    std::uint32_t w = (31u << 26) | (4u << 21) | (5u << 16) | (6u << 11) | (444u << 1);
    EncodedInstruction inst(w);
    EXPECT_EQ(cpu->findInstructionID(inst), InstructionID::E_OR);
}

// XOR lives at opcd=31, xo10=316.
TEST_F(InstructionTest, FindID_ExtendedXO10_XOR)
{
    std::uint32_t w = (31u << 26) | (3u << 21) | (3u << 16) | (3u << 11) | (316u << 1);
    EncodedInstruction inst(w);
    EXPECT_EQ(cpu->findInstructionID(inst), InstructionID::E_XOR);
}

// An unknown opcode (0x3F = 63 has only floating-point instructions; using an
// all-zeros non-matching extended word) must throw InterpreterException.
// RISK NOTE: executeInstruction catches this and silently skips the instruction
// (see the decode-cache path and m_handlers table). The throw here is only
// visible at the findInstructionID level. That means an unknown instruction in
// the run loop is silently skipped – a potential silent-corruption risk if
// guest code lands on an unsupported encoding.
TEST_F(InstructionTest, FindID_UnknownInstruction_Throws)
{
    // opcd=63, xo5=0, xo9=0, xo10=0 – this combination matches no INSTR() entry
    // that isn't a valid FP instruction.  Use opcd=0, which is never valid.
    EncodedInstruction inst(0x00000001u); // opcd=0, some junk below
    EXPECT_THROW(cpu->findInstructionID(inst), Core::InterpreterException);
}

// All-zero word: opcd=0, should throw InterpreterException from findInstructionID.
TEST_F(InstructionTest, FindID_AllZeroWord_Throws)
{
    EncodedInstruction inst(0u);
    EXPECT_THROW(cpu->findInstructionID(inst), Core::InterpreterException);
}

// ─────────────────────────────────────────────────────────────────────────────
//  executeInstruction – decode-cache path
// ─────────────────────────────────────────────────────────────────────────────

// First call populates the decode cache; second call hits it.  The result must
// be identical for both.
TEST_F(InstructionTest, ExecuteInstruction_DecodeCacheHit)
{
    // ADDI r5, r0, 42 (ra=0 → result = 0 + 42)
    EncodedInstruction inst(makeAddiWord(5, 0, 42));

    // First execution (cache miss)
    cpu->executeInstruction(inst);
    EXPECT_EQ(cpu->m_gpr[5], 42u);

    // Reset and re-run (cache hit path)
    cpu->reset();
    cpu->executeInstruction(inst);
    EXPECT_EQ(cpu->m_gpr[5], 42u);
}

// A cache-collision scenario: execute two different instructions whose
// (raw * 2654435761) >> 16 values could map to the same slot.  The simplest
// test is two different instruction words executed alternately.
TEST_F(InstructionTest, ExecuteInstruction_AlternatingWords_CorrectResults)
{
    // ADDI r5, r0, 10
    EncodedInstruction inst_a(makeAddiWord(5, 0, 10));
    // ADDI r6, r0, 20
    EncodedInstruction inst_b(makeAddiWord(6, 0, 20));

    cpu->executeInstruction(inst_a);
    cpu->executeInstruction(inst_b);
    EXPECT_EQ(cpu->m_gpr[5], 10u);
    EXPECT_EQ(cpu->m_gpr[6], 20u);

    // Run them again (both should be in-cache now)
    cpu->reset();
    cpu->executeInstruction(inst_a);
    cpu->executeInstruction(inst_b);
    EXPECT_EQ(cpu->m_gpr[5], 10u);
    EXPECT_EQ(cpu->m_gpr[6], 20u);
}

// executeInstruction of an unknown opcode should NOT crash: InterpreterException
// is thrown internally, but the instruction loop would catch it.  Verify that
// the exception propagates out when called directly (since we're not in step()).
TEST_F(InstructionTest, ExecuteInstruction_UnknownOpcode_Throws)
{
    // opcd=0 is never valid
    EncodedInstruction inst(0x00000001u);
    EXPECT_THROW(cpu->executeInstruction(inst), Core::InterpreterException);
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateCR0
// ─────────────────────────────────────────────────────────────────────────────

// When rc=0 and forceUpdate=false, CR0 must remain unchanged.
TEST_F(InstructionTest, UpdateCR0_RcZero_NoForce_CR0Unchanged)
{
    cpu->m_cr.cr0 = 0b1010; // set some bits
    EncodedInstruction inst(0);
    inst.rc = 0;
    cpu->updateCR0(42, inst, /*forceUpdate=*/false);
    EXPECT_EQ(cpu->m_cr.cr0, 0b1010u);
}

// When rc=1, a positive result sets GT (Positive).
TEST_F(InstructionTest, UpdateCR0_RcOne_PositiveResult_SetsGT)
{
    EncodedInstruction inst(0);
    inst.rc = 1;
    cpu->updateCR0(1, inst);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive, Core::ConditionRegisterFlag::Positive);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative, 0u);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero, 0u);
}

// When rc=1, zero result sets EQ (Zero).
TEST_F(InstructionTest, UpdateCR0_RcOne_ZeroResult_SetsEQ)
{
    EncodedInstruction inst(0);
    inst.rc = 1;
    cpu->updateCR0(0, inst);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero, Core::ConditionRegisterFlag::Zero);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive, 0u);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative, 0u);
}

// When rc=1, negative result sets LT (Negative).
TEST_F(InstructionTest, UpdateCR0_RcOne_NegativeResult_SetsLT)
{
    EncodedInstruction inst(0);
    inst.rc = 1;
    cpu->updateCR0(-5, inst);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Negative, Core::ConditionRegisterFlag::Negative);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Positive, 0u);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero, 0u);
}

// When XER.SO is set, the SO bit of CR0 must be copied in.
TEST_F(InstructionTest, UpdateCR0_XER_SO_Propagates_To_CR0_SO)
{
    EncodedInstruction inst(0);
    inst.rc = 1;
    cpu->m_xer.so = 1;
    cpu->updateCR0(1, inst);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::SummaryOverflow, Core::ConditionRegisterFlag::SummaryOverflow);
}

// forceUpdate=true must update CR0 even with rc=0.
TEST_F(InstructionTest, UpdateCR0_ForceUpdate_UpdatesEvenWithRcZero)
{
    EncodedInstruction inst(0);
    inst.rc = 0;
    cpu->updateCR0(0, inst, /*forceUpdate=*/true);
    EXPECT_EQ(cpu->m_cr.cr0 & Core::ConditionRegisterFlag::Zero, Core::ConditionRegisterFlag::Zero);
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateCR1
// ─────────────────────────────────────────────────────────────────────────────

// When rc=0, CR1 must remain unchanged regardless of FPSCR.
TEST_F(InstructionTest, UpdateCR1_RcZero_CR1Unchanged)
{
    cpu->m_cr.cr1 = 0b1111u;
    cpu->m_fpscr.fx = 1;
    EncodedInstruction inst(0);
    inst.rc = 0;
    cpu->updateCR1(inst);
    EXPECT_EQ(cpu->m_cr.cr1, 0b1111u);
}

// When rc=1 and FPSCR.FX is set, CR1 bit 0 (Negative/FX) should be set.
TEST_F(InstructionTest, UpdateCR1_RcOne_FX_SetsBit)
{
    EncodedInstruction inst(0);
    inst.rc = 1;
    cpu->m_fpscr.fx = 1;
    cpu->updateCR1(inst);
    EXPECT_NE(cpu->m_cr.cr1 & Core::ConditionRegisterFlag::Negative, 0u);
}

// When rc=1 and all FPSCR exception bits are clear, CR1 must be zero.
TEST_F(InstructionTest, UpdateCR1_RcOne_NoFPSCRBits_CR1Zero)
{
    EncodedInstruction inst(0);
    inst.rc = 1;
    // All FPSCR bits are already zero from reset()
    cpu->updateCR1(inst);
    EXPECT_EQ(cpu->m_cr.cr1, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateOverflow (bool overload)
// ─────────────────────────────────────────────────────────────────────────────

// When oe=0, XER.OV and XER.SO must not change regardless of overflow param.
TEST_F(InstructionTest, UpdateOverflow_Bool_OEZero_XERUnchanged)
{
    EncodedInstruction inst(0);
    inst.oe = 0;
    cpu->updateOverflow(/*overflow=*/true, inst);
    EXPECT_EQ(cpu->m_xer.ov, 0u);
    EXPECT_EQ(cpu->m_xer.so, 0u);
}

// When oe=1 and overflow=true, both OV and SO must be set.
TEST_F(InstructionTest, UpdateOverflow_Bool_OEOne_Overflow_SetsOVandSO)
{
    EncodedInstruction inst(0);
    inst.oe = 1;
    cpu->updateOverflow(/*overflow=*/true, inst);
    EXPECT_EQ(cpu->m_xer.ov, 1u);
    EXPECT_EQ(cpu->m_xer.so, 1u);
}

// When oe=1 and overflow=false, OV is cleared.  But SO is sticky – once set
// it stays set even when OV is cleared.
TEST_F(InstructionTest, UpdateOverflow_Bool_OEOne_NoOverflow_ClearsOV_SOSticky)
{
    EncodedInstruction inst(0);
    inst.oe = 1;
    // First set SO
    cpu->m_xer.so = 1;
    cpu->updateOverflow(/*overflow=*/false, inst);
    EXPECT_EQ(cpu->m_xer.ov, 0u);
    EXPECT_EQ(cpu->m_xer.so, 1u); // sticky: stays set
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateOverflow (signed-addition overload)
// ─────────────────────────────────────────────────────────────────────────────

// INT_MAX + 1 overflows: same-sign operands, opposite-sign result.
TEST_F(InstructionTest, UpdateOverflow_SignedAdd_PositiveOverflow_SetsOV)
{
    EncodedInstruction inst(0);
    inst.oe = 1;
    const std::int32_t a = 0x7FFFFFFF;
    const std::int32_t b = 1;
    const std::int32_t result = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    cpu->updateOverflow(a, b, result, inst);
    EXPECT_EQ(cpu->m_xer.ov, 1u);
    EXPECT_EQ(cpu->m_xer.so, 1u);
}

// INT_MIN + (-1) overflows: same-sign (negative) operands, positive result.
TEST_F(InstructionTest, UpdateOverflow_SignedAdd_NegativeOverflow_SetsOV)
{
    EncodedInstruction inst(0);
    inst.oe = 1;
    const std::int32_t a = static_cast<std::int32_t>(0x80000000u);
    const std::int32_t b = -1;
    const std::int32_t result = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    cpu->updateOverflow(a, b, result, inst);
    EXPECT_EQ(cpu->m_xer.ov, 1u);
}

// 10 + 20 = 30: no overflow.
TEST_F(InstructionTest, UpdateOverflow_SignedAdd_NoOverflow_ClearsOV)
{
    EncodedInstruction inst(0);
    inst.oe = 1;
    cpu->updateOverflow(10, 20, 30, inst);
    EXPECT_EQ(cpu->m_xer.ov, 0u);
}

// Mixed signs never overflow: positive + negative result lies in range.
TEST_F(InstructionTest, UpdateOverflow_SignedAdd_MixedSigns_NoOverflow)
{
    EncodedInstruction inst(0);
    inst.oe = 1;
    cpu->updateOverflow(0x7FFFFFFF, -1, 0x7FFFFFFE, inst);
    EXPECT_EQ(cpu->m_xer.ov, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
//  syscallFor – cache invalidation
// ─────────────────────────────────────────────────────────────────────────────

// After registering a syscall the cache should return the right handler.
TEST_F(InstructionTest, SyscallFor_RegisteredHandler_Returned)
{
    static bool called = false;
    const std::string sym = "__test_dispatch_sym__";
    Core::syscallHandler.registerSyscall(sym, [](Core::Interpreter &) { called = true; });

    const auto fn = cpu->syscallFor(sym);
    ASSERT_NE(fn, nullptr);
    fn(*cpu);
    EXPECT_TRUE(called);

    // Cleanup
    Core::syscallHandler.syscallTable.erase(sym);
    ++Core::syscallHandler.version;
    called = false;
}

// An unregistered name must return nullptr.
TEST_F(InstructionTest, SyscallFor_UnregisteredHandler_ReturnsNull)
{
    const std::string sym = "__test_no_such_syscall_xyz__";
    const auto fn = cpu->syscallFor(sym);
    EXPECT_EQ(fn, nullptr);
}

// After a new registration (version bump) the cache must be invalidated and
// the new handler returned.
TEST_F(InstructionTest, SyscallFor_CacheInvalidatedAfterNewRegistration)
{
    static int callCount = 0;
    const std::string sym = "__test_cache_invalidate__";

    // Prime the cache with nullptr (sym not yet registered)
    EXPECT_EQ(cpu->syscallFor(sym), nullptr);

    // Register – bumps version
    Core::syscallHandler.registerSyscall(sym, [](Core::Interpreter &) { ++callCount; });

    // Cache must be stale now; next call should return the new handler.
    const auto fn = cpu->syscallFor(sym);
    ASSERT_NE(fn, nullptr);
    fn(*cpu);
    EXPECT_EQ(callCount, 1);

    // Cleanup
    Core::syscallHandler.syscallTable.erase(sym);
    ++Core::syscallHandler.version;
    callCount = 0;
}
