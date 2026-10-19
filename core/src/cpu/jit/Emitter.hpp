/*
** EPITECH PROJECT, 2026
** core
** File description:
** Emitter -- x86-64 code generation for one PowerPC block (internal to the recompiler)
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>
#include <xbyak/xbyak.h>

#include "cpu/jit/X64Jit.hpp"
#include "cpu/types/EncodedInstruction.hpp"

namespace Core::Jit {
    constexpr std::uint32_t kInstructionBytes = 4;
    constexpr unsigned kTableBits = 20;
    constexpr std::uint32_t kTableMask = (1u << kTableBits) - 1;

    // pc -> code cache probed by the lookup stub. Empty slots hold an unaligned pc and the miss
    // stub, so even a stray match exits cleanly.
    struct TableEntry {
            std::uint32_t pc;
            std::uint32_t reserved;
            const void *code;
    };
    static_assert(sizeof(TableEntry) == 16, "the lookup stub scales the slot index by 16");
    constexpr std::uint32_t kEmptyPc = ~0u;

    // Per-block data the block's code addresses RIP-relative.
    struct BlockSlot {
            std::uint64_t executions;
    };

    // A direct jump to the block at `target`, patched once that block is compiled. `fallback`
    // reaches it through the lookup table.
    struct Link {
            std::uint32_t target;
            std::uint8_t *jump; // E9 rel32
            const std::uint8_t *fallback;
    };

    // Shared code at the start of the buffer, kept across flushes.
    struct Stubs {
            void (*enter)(Context *, std::uint32_t){};
            const void *lookup{}, *exit{}, *miss{}, *calloutExit{}, *spinExit{}, *hook{};
    };

    // Where generated code finds the guest state: displacements from rbx (&Interpreter::m_gpr[0])
    // and the guest memory map.
    struct GuestLayout {
            std::int32_t cpu{}, pc{}, nextPc{}, cr{}, xer{}, lr{}, ctr{}, fpr{}, ps1{}, gqr{}, fpscr{}, reserveValid{}, reserveAddr{};
            std::uint32_t ramBytes{};
            const TableEntry *table{};
            const void *memory{}; // Core::Memory, for slow-path helpers
            bool profile{};
            std::uint64_t spinThreshold{~std::uint64_t{0}}; // ~0: spin-yield disabled
            std::uint32_t nativeMask{~0u}; // Family bits allowed to compile natively (WEMU_JIT_CALLOUT)
    };

    // Instruction families that can be forced back to call-outs to bisect a recompiler bug.
    enum Family : std::uint32_t {
        FamilyInteger = 1u << 0,
        FamilyCompare = 1u << 1,
        FamilyLoad = 1u << 2,
        FamilyStore = 1u << 3,
        FamilyBranch = 1u << 4,
        FamilySystem = 1u << 5, // SPR, CR moves and logic
        FamilyFloat = 1u << 6,
        FamilyPaired = 1u << 7,
    };

    // Emits one block: entry checks, each instruction natively or through its call-out, then the
    // chain to the next block. Exits that leave the block early are emitted out of line after the
    // block body.
    class Emitter : public Xbyak::CodeGenerator {
        public:
            Emitter(std::uint8_t *at, std::size_t capacity, const GuestLayout &layout, const Stubs &stubs);

            void emitStubs(Stubs &stubs, const TableEntry *table);
            void emitBlock(std::uint32_t pc, std::span<const GuestInstruction> instrs, bool stores, BlockSlot *slot);

            static Xbyak::RegExp at(const void *address) { return Xbyak::RegExp(Xbyak::util::rip) + Xbyak::RegExp(address); }
            // The block's direct exits, valid after emitBlock.
            [[nodiscard]] std::vector<Link> links() const;
            // Instructions of the block left to their call-out.
            [[nodiscard]] std::uint32_t callouts() const { return m_callouts; }

        private:
            // Per-family native emitters; false leaves the instruction to its call-out. A branch
            // that returns true has ended the block (exitTo / exitIndirect on every path).
            bool emitInteger(const EncodedInstruction &i, InstructionID id);
            bool emitSystem(const EncodedInstruction &i, InstructionID id);
            bool emitMemory(const EncodedInstruction &i, InstructionID id);
            bool emitBranch(const EncodedInstruction &i, InstructionID id);
            bool emitFloat(const EncodedInstruction &i, InstructionID id);
            bool emitFloatMemory(const EncodedInstruction &i, InstructionID id);
            bool emitPaired(const EncodedInstruction &i, InstructionID id);
            void emitCallout(const GuestInstruction &instr);
            // Runs `instr` (at `pc`, block index `index`) through its call-out; leaves the block if
            // it asks to, otherwise continues after the call.
            void calloutCall(const GuestInstruction &instr, std::uint32_t pc, std::uint32_t index);

            // Guest state operands.
            Xbyak::Address gpr(unsigned r) { return dword[rbx + static_cast<int>(r * 4)]; }
            Xbyak::Address cr() { return dword[rbx + m_layout.cr]; }
            Xbyak::Address xer() { return dword[rbx + m_layout.xer]; }
            Xbyak::Address lr() { return dword[rbx + m_layout.lr]; }
            Xbyak::Address ctr() { return dword[rbx + m_layout.ctr]; }
            Xbyak::Address fpr(unsigned r) { return qword[rbx + m_layout.fpr + static_cast<int>(r * 8)]; }
            Xbyak::Address ps1(unsigned r) { return qword[rbx + m_layout.ps1 + static_cast<int>(r * 8)]; }

            // CR0 from the signed result in eax (and XER[SO]); clobbers ecx, edx.
            void recordCr0();
            // CR field `field` = ecx (4 bits); clobbers ecx, edx.
            void setCrField(unsigned field);
            // XER[CA] = host CF / !CF; clobbers ecx, edx (not eax).
            void setCarry(bool inverted = false);
            void loadCarry(); // host CF = XER[CA]
            void recordCr1(const EncodedInstruction &i); // CR1 from FPSCR when Rc
            void roundToSingle(const Xbyak::Xmm &x);
            void flipSign(const Xbyak::Xmm &x); // clobbers rax, xmm5
            // Replays the instruction through its call-out when a result is NaN; returns the label
            // to bind after the instruction's writes.
            Xbyak::Label &guardNaN(std::initializer_list<Xbyak::Xmm> results);
            // xmm0 = the interpreter's double-precision result of one lane; clobbers xmm1, xmm2, xmm5, rax.
            void arithmetic(InstructionID id, const Xbyak::Address &a, const Xbyak::Address &b, const Xbyak::Address &c);
            void compareFloat(const Xbyak::Address &a, const Xbyak::Address &b, unsigned field, bool ordered);
            void quantized(const EncodedInstruction &i, InstructionID id);

            // Guest memory access with the effective address in ecx. Loads leave the value in eax
            // (rax for 8 bytes), zero-extended, or sign-extended 16-bit values when `signExtend`.
            // Stores take the value in eax/rax; `tail` emits the rest of the instruction (address
            // update), also run before leaving when the store wrote decoded code. Unmapped
            // addresses leave the block before the instruction. ecx survives both. With
            // `replayOnCodeWrite`, a store that wrote decoded code replays the whole instruction in
            // the interpreter instead (for instructions that store more after this one).
            void load(unsigned bytes, bool signExtend = false);
            void multiple(const EncodedInstruction &i, bool isStore); // lmw / stmw
            // ecx = (RA|0) + D or (RA|0) + RB; update forms always add RA.
            void effectiveAddress(const EncodedInstruction &i, bool indexed, bool update);
            void store(unsigned bytes, const std::function<void()> &tail = {}, bool replayOnCodeWrite = false);

            // Block ends: spin-yield bookkeeping, then a linkable jump to a constant pc, or an inline
            // lookup of the pc in eax.
            void spinCheck(std::optional<std::uint32_t> target);
            void exitTo(std::uint32_t target);
            void exitIndirect();

            // Out-of-line exits for the current instruction.
            Xbyak::Label &exitBefore(); // Suffix: the interpreter runs this instruction and the rest
            Xbyak::Label &exitAfter(); // Done: code was written, m_pc = next instruction
            Xbyak::Label &newLabel() { return m_labels.emplace_back(); }
            void cold(std::function<void()> code) { m_cold.push_back(std::move(code)); }
            void callHost(const void *fn);

            const GuestLayout &m_layout;
            const Stubs &m_stubs;
            std::deque<Xbyak::Label> m_labels;
            std::deque<std::function<void()>> m_cold; // may grow while emitted
            struct PendingLink {
                    std::uint32_t target;
                    const Xbyak::Label *jump, *fallback;
            };
            std::vector<PendingLink> m_pendingLinks;
            std::uint32_t m_blockPc{}, m_pc{}, m_index{}, m_count{}, m_callouts{};
            GuestInstruction m_current{};
            bool m_stores{};
    };
} // namespace Core::Jit
