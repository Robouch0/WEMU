/*
** EPITECH PROJECT, 2025
** core
** File description:
** Interpreter
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <vector>

#include "Registers.hpp"
#include "binary/Binary.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "cpu/types/Instruction.hpp"
#include "gfx/Renderer.hpp"
#include "hle/H264.hpp"
#include "hle/Scheduler.hpp"
#include "utils/BeDecoder.hpp"
#include "utils/Logger.hpp"

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
                m_h264.reset();
                m_blockCache.clear();
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
            std::atomic<std::uint32_t> m_controllerMask{0};
            std::uint32_t m_vpadPreviousHold{0};
            std::uint64_t m_vpadReadCount{0};
            bool m_hle_redirected{false};
            bool m_interruptsDisabled{false}; // OSDisableInterrupts: defers timeslice preemption
            bool m_reserveValid{false}; // lwarx reservation; cleared on context switch
            std::uint32_t m_reserveAddr{0};

            Renderer *m_renderer = nullptr; // Window — set after construction
            std::unordered_map<std::uint32_t, HookFn> m_hooks; // PPC addr → intercept fn
            std::uint32_t m_hooks_min{0xFFFFFFFFu}; // Opt C: hook address range
            std::uint32_t m_hooks_max{0u};

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
            // stores end blocks so rewritten following instructions are fetched again.
            struct DecodedInstr {
                    RawHandler fn;
                    EncodedInstruction instr;
            };
            struct Block {
                    std::vector<DecodedInstr> instrs; // straight-line run, terminator (branch) last
#ifdef WEMU_HAS_LLVM
                    std::vector<std::uint32_t> nativeWords; // supported prefix, empty when not eligible
                    unsigned nativePrefixLength{}; // includes ineligible single-instruction prefixes
#endif
            };
            std::unordered_map<std::uint32_t, Block> m_blockCache; // keyed by memory-relative PC
            bool m_jitEnabled{false};
            std::uint64_t m_retired{0}; // total instructions retired (WEMU_IPS throughput reporting)

            // Build (or fetch) the block starting at the current m_pc. Blocks end at a control-flow
            // instruction, a hooked address, an illegal word, or a length cap.
            const Block &getBlock(Utils::BeDecoder &decoder, std::uint32_t startPc);
            // Execute a whole block; returns the number of instructions retired (for slice accounting).
            std::uint32_t runBlock(const Block &block);

            // Import sentinel (st_value >= 0xC0000000) -> symbol name, for BCTR HLE dispatch.
            std::unordered_map<std::uint32_t, const std::string *> m_importBySentinel{};
    };


} // namespace Core
