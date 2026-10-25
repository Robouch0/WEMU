/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "Registers.hpp"
#include "binary/Binary.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"
#include "gfx/Renderer.hpp"
#include "hle/H264.hpp"
#include "hle/LockedCache.hpp"
#include "hle/MvPlayer.hpp"
#include "hle/Scheduler.hpp"
#include "hle/SystemConfig.hpp"
#include "utils/BeDecoder.hpp"
#include "utils/Logger.hpp"

#ifdef WEMU_HAS_LLVM
    #include "cpu/recompiler/LlvmBackend.hpp"
#endif
#ifdef WEMU_HAS_X64_JIT
    #include "cpu/jit/X64Jit.hpp"
#endif

namespace Core {
    static constexpr std::uint32_t INSTR_SIZE = sizeof(std::uint32_t);

    // Sentinel return address installed in LR before entering the module. When the entry function
    // returns (blr) to it, the program has finished its top-level call and we stop cleanly.
    static constexpr std::uint32_t RETURN_SENTINEL = 0x0FFFFFFCu;
    // Return address seeded into LR for AX frame callbacks: landing here means the callback
    // finished; the pump then runs the next callback or parks the synthetic AX thread.
    static constexpr std::uint32_t AX_FRAME_SENTINEL = 0x0FFFFFF4u;
    // Return address seeded into LR for deferred async completion callbacks (nn::fp login, etc.):
    // landing here means one queued callback finished; the async pump runs the next or parks.
    static constexpr std::uint32_t ASYNC_CB_SENTINEL = 0x0FFFFFF0u;

    class InterpreterException final : public Core::Exception {
        public:
            explicit InterpreterException(const std::string &errorMessage) : Core::Exception("InterpreterException", errorMessage) {}

            ~InterpreterException() override = default;
    };

    class Interpreter {
        public:
            explicit Interpreter(Core::Binary binary);

            ~Interpreter() = default;

            void run();

            InstructionID findInstructionID(const EncodedInstruction &instr);

            void executeInstruction(const EncodedInstruction &instr);

            template<typename T>
            T readArgs(size_t index)
            {
                return m_gpr[3 + index];
            }

            void writeReturnValue(const std::uint32_t val) { m_gpr[3] = val; }

            void debugDumpGPR()
            {
                if constexpr (Utils::Log::kLevel <= Utils::Log::Level::Trace) {
                    std::cout << "==== GPR Dump ====" << std::endl;
                    for (int i = 0; i < 32; ++i)
                        std::cout << std::format("r{:02d} : 0x{:08X}  ({})", i, m_gpr[i], m_gprSigned[i]) << std::endl;
                    std::cout << "==================" << std::endl;

                    std::cout << "==== CR Dump  ====" << std::endl;
                    for (int i = 0; i < 8; ++i) {
                        const std::uint32_t field = (m_cr.raw >> ((7 - i) * 4)) & 0xF;
                        std::cout << std::format("cr{} : 0x{:X}  [{}{}{}{}]", i, field, (field & ConditionRegisterFlag::Negative) ? "LT " : "   ",
                                                 (field & ConditionRegisterFlag::Positive) ? "GT " : "   ",
                                                 (field & ConditionRegisterFlag::Zero) ? "EQ " : "   ",
                                                 (field & ConditionRegisterFlag::SummaryOverflow) ? "SO" : "  ")
                                  << std::endl;
                    }
                    std::cout << "==== CTR Dump  ====" << std::endl;
                    std::cout << std::format("ctr {:X}", m_ctr) << std::endl;
                    std::cout << "==================" << std::endl;

                    std::cout << "268437924 MEM -> " << std::hex << this->m_memory.read<uint32_t>(268437924) << std::dec << std::endl;
                }
            }

            void reset()
            {
                m_vpadLcdMode = 0xFF;
                m_h264.reset();
                m_mvPlayer.reset();
                m_lockedCache = {};
                clearBlockCache();
                m_observedPcs.clear();
                m_failed = false;
                m_pc = 0;
                m_nextPc = 0;
                m_cr = {};
                m_lr = 0;
                m_ctr = 0;
                std::fill(std::begin(m_gpr), std::end(m_gpr), 0u);
                m_xer = {};
                std::fill(std::begin(m_fpr), std::end(m_fpr), 0.0f);
                m_fpscr = {};
            }

        public:
            [[nodiscard]] bool step(Utils::BeDecoder &decoder, const std::uint32_t ppc_pc);

            void initInstructionMap();

            void updateCR0(const std::int32_t &result, const EncodedInstruction &instr, const bool forceUpdate = false);

            void updateCR1(const EncodedInstruction &instr) noexcept;

            /**
             * @brief Updates XER overflow bits based on a precomputed overflow condition.
             *        Use this overload for instructions with custom overflow logic (e.g. DIVW, DIVWU).
             *
             * @param overflow Precomputed overflow condition
             * @param instr    Encoded instruction (used to check OE bit)
             */
            void updateOverflow(bool overflow, const EncodedInstruction &instr);

            /**
             * @brief Updates XER overflow bits for signed addition-based instructions.
             *        Overflow is detected when two operands of the same sign produce a result of opposite sign.
             *        Use this overload for ADD, ADDC, ADDE, ADDZE, ADDME and their variants.
             *
             * @param a      First operand (signed)
             * @param b      Second operand (signed)
             * @param result Result of the addition (signed)
             * @param instr  Encoded instruction (used to check OE bit)
             */
            void updateOverflow(const std::int32_t &a, const std::int32_t &b, const std::int32_t &result, const EncodedInstruction &instr);

            void stop() { m_running = false; }
            [[nodiscard]] bool failed() const { return m_failed; }

#define INSTR(name, ...) friend void Core::Instruction::name(Core::Interpreter &, const EncodedInstruction &);
#include "cpu/tables/cpu_instructions.anh"
#undef INSTR

            using HookFn = std::function<void(Interpreter &)>;

            std::atomic<bool> m_running{true};
            bool m_failed{false};
            std::shared_ptr<H264::State> m_h264;
            std::shared_ptr<MvPlayer::State> m_mvPlayer;
            std::atomic<std::uint32_t> m_controllerMask{0};
            std::uint32_t m_vpadPreviousHold{0};
            std::uint32_t m_vpadLcdMode{0xFF};
            std::uint64_t m_vpadReadCount{0};
            bool m_hle_redirected{false};
            bool m_interruptsDisabled{false}; // OSDisableInterrupts: defers timeslice preemption
            bool m_reserveValid{false}; // lwarx reservation; cleared on context switch
            std::uint32_t m_reserveAddr{0};

            Renderer *m_renderer = nullptr; // Window — set after construction
            std::unordered_map<std::uint32_t, HookFn> m_hooks; // PPC addr → intercept fn
            std::uint32_t m_hooks_min{0xFFFFFFFFu}; // Opt C: hook address range
            std::uint32_t m_hooks_max{0u};
            // Hooks are spread over the whole code range, so most blocks would otherwise look their PC
            // up in m_hooks. One bit per (address >> 2) & 0xFFFF screens them first; it is rebuilt
            // whenever the hook count changes (HLE may add hooks while the guest runs).
            [[nodiscard]] bool hooked(const std::uint32_t ppc)
            {
                if (ppc < m_hooks_min || ppc > m_hooks_max)
                    return false;
                if (m_hookFilterCount != m_hooks.size()) {
                    m_hookFilter.fill(0);
                    for (const auto &[address, hook]: m_hooks)
                        m_hookFilter[(address >> 2 & 0xFFFF) / 64] |= std::uint64_t{1} << (address >> 2 & 63);
                    m_hookFilterCount = m_hooks.size();
                }
                return (m_hookFilter[(ppc >> 2 & 0xFFFF) / 64] >> (ppc >> 2 & 63) & 1) && m_hooks.contains(ppc);
            }
            std::array<std::uint64_t, 1024> m_hookFilter{};
            std::size_t m_hookFilterCount{~std::size_t{0}};

            Core::Binary m_binary;
            // Data accesses, instruction fetch, and diagnostics share the binary's owned RAM.
            Core::Memory &m_memory;

            std::uint32_t m_pc{};
            std::uint32_t m_nextPc{};

            Core::ConditionRegister m_cr{};
            std::uint32_t m_lr{}; // Link Register
            std::uint32_t m_ctr{}; // Counter Register
            union {
                    std::uint32_t m_gpr[32]{};
                    std::int32_t m_gprSigned[32];
            }; // General Purpose Registers (unsigned/signed)
            Core::FixedPointExceptionRegister m_xer{};
            double m_fpr[32]{}; // Floating-Point Registers (ps0 slot for paired single)
            double m_ps1[32]{}; // Paired-single second slot (ps1)
            std::uint32_t m_gqr[8]{}; // Graphics Quantization Registers (SPR 912-919)
            Core::FloatingPointStatusAndControlRegister m_fpscr{};

            Core::Scheduler m_scheduler{}; // cooperative guest-thread scheduler
            Core::LockedCacheState m_lockedCache{};
            Core::SystemConfig m_systemConfig{Core::SystemConfig::fromEnvironment()};

            std::map<std::uint32_t, std::vector<InstructionInfo>> m_instructionMap{};

            // Decode cache. findInstructionID() is a pure function of the 32-bit instruction word
            // (it only inspects the encoding), but it costs a std::map lookup plus a linear scan of
            // every instruction sharing that primary opcode (opcd 31 alone has ~100 candidates). The
            // same words repeat billions of times inside guest loops, so we memoise the decode in a
            // direct-mapped cache keyed by the exact word. A hit is a single compare + branch; a
            // miss falls back to findInstructionID() and fills the slot. This is the first step of
            // the interpreter-speedup path (decode-once) and keeps the existing table as the source
            // of truth. Empty slots hold raw==0, which is the illegal word and never reaches decode,
            // so it can never collide with a real lookup.
            struct DecodeCacheEntry {
                    std::uint32_t raw{0};
                    InstructionID id{};
            };
            static constexpr std::uint32_t kDecodeCacheSize = 1u << 16; // 64K slots, 8 bytes each
            std::vector<DecodeCacheEntry> m_decodeCache{kDecodeCacheSize};

            // Raw function-pointer dispatch table, indexed by InstructionID. INSTRUCTIONARRAY stores
            // each handler in a std::function (type-erased: an indirect call through a vtable-like
            // wrapper, run for every instruction). The handlers are all plain free functions, so we
            // extract their bare pointers once at init and call through this instead — a direct
            // indirect call with no wrapper.
            using RawHandler = void (*)(Core::Interpreter &, const EncodedInstruction &);
            std::vector<RawHandler> m_handlers;

            // --- Light JIT: pre-decoded basic-block cache (WEMU_JIT=1) -------------------------------
            //
            // The interpreter re-fetches and re-decodes every instruction every time it runs, even
            // inside tight guest loops. A "basic block" is a straight-line run of instructions with a
            // single entry and a branch at the end. We decode each block ONCE into a flat array of
            // (handler, decoded instruction) and cache it by start PC; re-executing the block then
            // skips fetch, decode, and most of the per-instruction bookkeeping in run(). This is the
            // honest "light JIT" — no native code generation, so it stays portable and readable and
            // the existing instruction table remains the source of truth — and it is the natural next
            // step after the decode cache. It is opt-in while it proves out, so the working boot path
            // is never at risk. Cached words are revalidated against live guest memory;
            // interpreted blocks end at stores; native lookahead guards code writes.
            struct DecodedInstr {
                    RawHandler fn;
                    EncodedInstruction instr;
                    bool store{false}; // may write guest memory: leave the block if it hit decoded code
                    InstructionID id{};
            };
            struct Block {
                    std::vector<DecodedInstr> instrs; // straight-line run, terminator (branch) last
                    // Guest-order words of every validated instruction (instrs plus native lookahead),
                    // compared against live memory with one memcmp on each dispatch.
                    std::vector<std::uint32_t> guestWords;
                    bool stores{false}; // any instruction may write guest memory (spin detection)
                    mutable std::uint64_t validatedEpoch{0}; // Memory::codeEpoch() of the last word comparison
                    // Successor chaining: the blocks most recently dispatched after this one, so the
                    // common path skips the lookup table. Entries hold for one m_blockGeneration.
                    struct Successor {
                            std::uint32_t pc{~0u};
                            const Block *block{nullptr};
                            std::uint64_t generation{};
                    };
                    mutable std::array<Successor, 2> successors{};
#ifdef WEMU_HAS_LLVM
                    std::vector<std::uint32_t> nativeWords; // supported prefix, empty when not eligible
                    mutable Ppc::NativeBinding nativeCode; // Cache invalidation expires this binding.
                    mutable unsigned nativeVisits{}; // Admit hot decoded versions before entering the bounded native cache.
                    bool nativeLoop{}, nativeLookahead{};
                    unsigned nativePrefixLength{}; // includes ineligible single-instruction prefixes
#endif
            };
            std::unordered_map<std::uint32_t, Block> m_blockCache; // keyed by memory-relative PC
            std::uint64_t m_blockGeneration{1}; // advanced whenever a cached block is destroyed
            // Direct-mapped front of m_blockCache (map nodes are stable). Every erase/clear of the
            // map goes through getBlock()'s replacement or clearBlockCache() so no slot outlives its block.
            struct BlockLookup {
                    std::uint32_t pc{~0u};
                    Block *block{nullptr};
            };
            static constexpr std::uint32_t kBlockLookupSize = 1u << 16;
            std::vector<BlockLookup> m_blockLookup{kBlockLookupSize};
            static constexpr std::uint32_t blockLookupSlot(std::uint32_t pc) noexcept { return (pc >> 2) & (kBlockLookupSize - 1); }
            void clearBlockCache()
            {
                ++m_blockGeneration;
                m_blockCache.clear();
                std::fill(m_blockLookup.begin(), m_blockLookup.end(), BlockLookup{});
                m_memory.clearCodePages();
#ifdef WEMU_HAS_X64_JIT
                // Compiled code relies on the code-page marks that were just cleared.
                if (m_x64Jit)
                    m_x64Jit->flush();
#endif
            }
            std::vector<std::uint32_t> m_observedPcs; // absolute guest PCs that must reach the diagnostic dispatch
            bool m_jitEnabled{false};
            std::uint64_t m_retired{0}; // total instructions retired (WEMU_IPS throughput reporting)

            // Build (or fetch) the block starting at the current m_pc. Blocks end at a control-flow
            // instruction, a hooked address, an illegal word, or a length cap.
            const Block &getBlock(Utils::BeDecoder &decoder, std::uint32_t startPc, bool nativeLookahead = false);
            // Execute a whole block; returns the number of instructions retired (for slice accounting).
            std::uint32_t runBlock(const Block &block, std::size_t first = 0);
            // Reports a guest exception raised by the block instruction at m_pc. Returns true when it
            // killed the current thread or stopped the interpreter, so the block must not continue.
            bool handleBlockException(const Core::Exception &e);
#ifdef WEMU_HAS_X64_JIT
            std::unique_ptr<Jit::X64Jit> m_x64Jit; // WEMU_X64_JIT=1: compiled blocks
#endif

            // Import sentinel (st_value >= 0xC0000000) -> symbol name, for BCTR HLE dispatch.
            std::unordered_map<std::uint32_t, const std::string *> m_importBySentinel{};

            // HLE handler registered for a symbol name (sc, bctr to an import), or nullptr. Names
            // live in m_binary.symbols, so lookups in the string-keyed syscall table are cached by
            // name address until a registration changes the table.
            using SyscallFn = void (*)(Interpreter &);
            [[nodiscard]] SyscallFn syscallFor(const std::string &name);
            std::unordered_map<const std::string *, SyscallFn> m_syscallCache;
            std::uint64_t m_syscallCacheVersion{~std::uint64_t{0}};
    };


} // namespace Core
