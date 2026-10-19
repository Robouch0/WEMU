/*
** EPITECH PROJECT, 2026
** core
** File description:
** JitHarness -- differential tests of the x86-64 recompiler against the interpreter
*/

#pragma once

#ifdef WEMU_HAS_X64_JIT

    #include <array>
    #include <bit>
    #include <cstring>
    #include <format>
    #include <functional>
    #include <random>
    #include <span>
    #include <string>
    #include <vector>

    #include "TestFixture.hpp"
    #include "cpu/jit/X64Jit.hpp"

// Reproducible 32-bit random source (std::mt19937 yields unsigned long on this ABI).
struct Random {
        explicit Random(const unsigned seed) : engine(seed) {}
        std::uint32_t operator()() { return static_cast<std::uint32_t>(engine()); }
        std::mt19937 engine;
};

// Everything a block may change, doubles compared bit for bit.
struct GuestState {
        std::array<std::uint32_t, 32> gpr{};
        std::array<std::uint64_t, 32> fpr{}, ps1{};
        std::array<std::uint32_t, 8> gqr{};
        std::uint32_t cr{}, xer{}, lr{}, ctr{}, fpscr{}, pc{}, nextPc{}, reserveAddr{};
        bool reserveValid{};

        bool operator==(const GuestState &) const = default;

        static GuestState capture(const Core::Interpreter &cpu)
        {
            GuestState s;
            std::memcpy(s.gpr.data(), cpu.m_gpr, sizeof(cpu.m_gpr));
            std::memcpy(s.fpr.data(), cpu.m_fpr, sizeof(cpu.m_fpr));
            std::memcpy(s.ps1.data(), cpu.m_ps1, sizeof(cpu.m_ps1));
            std::memcpy(s.gqr.data(), cpu.m_gqr, sizeof(cpu.m_gqr));
            s.cr = cpu.m_cr.raw;
            s.xer = cpu.m_xer.raw;
            s.lr = cpu.m_lr;
            s.ctr = cpu.m_ctr;
            s.fpscr = cpu.m_fpscr.raw;
            s.pc = cpu.m_pc;
            s.nextPc = cpu.m_nextPc;
            s.reserveValid = cpu.m_reserveValid;
            s.reserveAddr = cpu.m_reserveAddr;
            return s;
        }

        void restore(Core::Interpreter &cpu) const
        {
            std::memcpy(cpu.m_gpr, gpr.data(), sizeof(cpu.m_gpr));
            std::memcpy(cpu.m_fpr, fpr.data(), sizeof(cpu.m_fpr));
            std::memcpy(cpu.m_ps1, ps1.data(), sizeof(cpu.m_ps1));
            std::memcpy(cpu.m_gqr, gqr.data(), sizeof(cpu.m_gqr));
            cpu.m_cr.raw = cr;
            cpu.m_xer.raw = xer;
            cpu.m_lr = lr;
            cpu.m_ctr = ctr;
            cpu.m_fpscr.raw = fpscr;
            cpu.m_pc = pc;
            cpu.m_nextPc = nextPc;
            cpu.m_reserveValid = reserveValid;
            cpu.m_reserveAddr = reserveAddr;
        }

        // First differing register, for failure messages.
        [[nodiscard]] std::string diff(const GuestState &o) const
        {
            for (int i = 0; i < 32; ++i) {
                if (gpr[i] != o.gpr[i])
                    return std::format("r{}: 0x{:08X} vs 0x{:08X}", i, gpr[i], o.gpr[i]);
                if (fpr[i] != o.fpr[i])
                    return std::format("f{}.ps0: 0x{:016X} vs 0x{:016X}", i, fpr[i], o.fpr[i]);
                if (ps1[i] != o.ps1[i])
                    return std::format("f{}.ps1: 0x{:016X} vs 0x{:016X}", i, ps1[i], o.ps1[i]);
            }
            for (int i = 0; i < 8; ++i)
                if (gqr[i] != o.gqr[i])
                    return std::format("gqr{}: 0x{:08X} vs 0x{:08X}", i, gqr[i], o.gqr[i]);
            const std::array<std::pair<const char *, std::pair<std::uint32_t, std::uint32_t>>, 9> scalars{{
                    {"cr", {cr, o.cr}},
                    {"xer", {xer, o.xer}},
                    {"lr", {lr, o.lr}},
                    {"ctr", {ctr, o.ctr}},
                    {"fpscr", {fpscr, o.fpscr}},
                    {"pc", {pc, o.pc}},
                    {"nextPc", {nextPc, o.nextPc}},
                    {"reserveAddr", {reserveAddr, o.reserveAddr}},
                    {"reserveValid", {reserveValid, o.reserveValid}},
            }};
            for (const auto &[name, values]: scalars)
                if (values.first != values.second)
                    return std::format("{}: 0x{:08X} vs 0x{:08X}", name, values.first, values.second);
            return "equal";
        }
};

class X64JitTest : public InstructionTest {
    protected:
        static constexpr std::uint32_t kApp = Core::Memory::MemoryMap::ApplicationCode;
        static constexpr std::uint32_t kCode = 0x00100000; // memory-relative offset of test code
        static constexpr std::uint32_t kData = 0x10000000; // guest address of the scratch data
        static constexpr std::uint32_t kDataBytes = 0x4000;
        static constexpr std::uint32_t kStack = Core::Memory::STACK_BASE + 0x1000; // compared stack window
        static constexpr std::uint32_t kCodeData = kApp + kCode + 0x400; // data sharing the code page
        static constexpr std::uint32_t kWindowBytes = 0x1000;

        struct Outcome {
                GuestState state;
                std::vector<std::uint8_t> data, stack, codeData;
                std::uint64_t retired{};
                bool running{}, failed{};
        };

        void SetUp() override
        {
            InstructionTest::SetUp();
            cpu->m_running = true;
            cpu->m_failed = false;
            cpu->m_scheduler.bootstrap(*cpu); // guest faults hit the main thread
            cpu->m_x64Jit = Core::Jit::X64Jit::create(*cpu);
            ASSERT_NE(cpu->m_x64Jit, nullptr);
            jit().sync(cpu->m_memory.codeEpoch(), cpu->m_hooks.size());
            std::vector<std::uint8_t> zero(kDataBytes, 0);
            std::memcpy(cpu->m_memory.hostPtr(kData, kDataBytes), zero.data(), kDataBytes);
        }

        void TearDown() override
        {
            cpu->m_x64Jit.reset();
            cpu->m_running = true;
            cpu->m_failed = false;
        }

        Core::Jit::X64Jit &jit() { return *cpu->m_x64Jit; }

        // Writes code at `at` followed by illegal zero words, which end any decoded block.
        void load(std::span<const std::uint32_t> words, const std::uint32_t at = kCode)
        {
            for (std::size_t i = 0; i < words.size() + 64; ++i)
                cpu->m_memory.write<std::uint32_t>(kApp + at + static_cast<std::uint32_t>(i * 4), i < words.size() ? words[i] : 0);
            cpu->m_memory.clearCodeWritten();
        }

        const Core::Interpreter::Block &block(const std::uint32_t pc)
        {
            Utils::BeDecoder reader{std::span<const char>(cpu->m_memory.getMemory())};
            return cpu->getBlock(reader, pc);
        }

        // Compiles (if needed) and runs compiled code from m_pc the way Interpreter::run() does,
        // until an exit; `limit` is the retired count at which the next block must not start.
        // Revalidation exits are handled here unless `revalidate` is false.
        Core::Jit::X64Jit::Exit enter(std::uint64_t &retired, const std::uint64_t limit, std::uint64_t &lastStore, const bool revalidate = true)
        {
            for (;;) {
                jit().sync(cpu->m_memory.codeEpoch(), cpu->m_hooks.size());
                if (!jit().prepare(cpu->m_pc)) {
                    const auto &b = block(cpu->m_pc);
                    std::vector<Core::Jit::GuestInstruction> guest;
                    for (const auto &i: b.instrs)
                        guest.push_back({i.instr.raw, i.id});
                    EXPECT_TRUE(jit().compile(cpu->m_pc, guest, b.stores));
                }
                const auto exit = jit().run(retired, limit, lastStore);
                if (exit.reason != Core::Jit::ExitReason::Revalidate || !revalidate)
                    return exit;
                cpu->m_pc = exit.pc;
                jit().revalidate(exit.pc);
            }
        }

        // Applies an exit to the interpreter state as Interpreter::run() does (spin-yield aside).
        void settle(const Core::Jit::X64Jit::Exit &exit, std::uint64_t &retired)
        {
            using Core::Jit::ExitReason;
            switch (exit.reason) {
                case ExitReason::Budget:
                case ExitReason::LookupMiss:
                case ExitReason::Revalidate:
                case ExitReason::Done:
                    cpu->m_pc = cpu->m_nextPc = exit.pc;
                    break;
                case ExitReason::Suffix:
                    cpu->m_pc = exit.pc;
                    retired += cpu->runBlock(block(exit.blockPc), exit.index);
                    break;
                default:
                    break;
            }
        }

        std::vector<std::uint8_t> bytes(const std::uint32_t address, const std::uint32_t size) const
        {
            std::vector<std::uint8_t> out(size);
            std::memcpy(out.data(), std::as_const(cpu->m_memory).hostPtr(address, size), size);
            return out;
        }

        Outcome capture(const std::uint64_t retired) const
        {
            return {GuestState::capture(*cpu),
                    bytes(kData, kDataBytes),
                    bytes(kStack, kWindowBytes),
                    bytes(kCodeData, kWindowBytes / 4),
                    retired,
                    cpu->m_running,
                    cpu->m_failed};
        }

        void restore(const Outcome &o)
        {
            o.state.restore(*cpu);
            std::memcpy(cpu->m_memory.hostPtr(kData, kDataBytes), o.data.data(), kDataBytes);
            std::memcpy(cpu->m_memory.hostPtr(kStack, kWindowBytes), o.stack.data(), kWindowBytes);
            std::memcpy(cpu->m_memory.hostPtr(kCodeData, kWindowBytes / 4), o.codeData.data(), kWindowBytes / 4);
            cpu->m_memory.clearCodeWritten();
            cpu->m_running = o.running;
            cpu->m_failed = o.failed;
        }

        // Runs the block at kCode once through runBlock and once compiled, from the same state, and
        // expects identical registers, scratch memory, retired count and next PC.
        void expectSameBlock(std::span<const std::uint32_t> words, const std::function<void()> &setup = {})
        {
            load(words);
            cpu->m_pc = cpu->m_nextPc = kCode;
            if (setup)
                setup();
            const Outcome start = capture(0);

            const auto &reference = block(kCode);
            const std::uint64_t referenceRetired = cpu->runBlock(reference);
            const Outcome expected = capture(referenceRetired);

            restore(start);
            std::uint64_t retired = 0, lastStore = 0;
            const auto exit = enter(retired, 1, lastStore);
            settle(exit, retired);
            const Outcome actual = capture(retired);

            const bool same = expected.state == actual.state && expected.retired == actual.retired && expected.running == actual.running &&
                              expected.failed == actual.failed && expected.data == actual.data && expected.stack == actual.stack &&
                              expected.codeData == actual.codeData;
            EXPECT_TRUE(same) << std::format("block 0x{:08X} ({} words): {}, retired {} vs {}, running {} vs {}, memory {}/{}/{}", words[0],
                                             words.size(), expected.state.diff(actual.state), expected.retired, actual.retired, expected.running,
                                             actual.running, expected.data == actual.data ? "same" : "DIFF",
                                             expected.stack == actual.stack ? "same" : "DIFF",
                                             expected.codeData == actual.codeData ? "same" : "DIFF");
            restore(start);
        }

        // Values that exercise carries, overflows, shifts and sign boundaries.
        static std::uint32_t value(Random &rng)
        {
            static constexpr std::uint32_t kSpecial[] = {0,          1,          2,          31,         32,        33,         63,
                                                         64,         0x7FFF,     0x8000,     0xFFFF,     0x10000,   0x7FFFFFFF, 0x80000000,
                                                         0x80000001, 0xFFFF0000, 0xFFFFFFFE, 0xFFFFFFFF, 0xC0000000};
            return (rng() & 3) == 0 ? kSpecial[rng() % std::size(kSpecial)] : static_cast<std::uint32_t>(rng());
        }

        // Random but reproducible integer state.
        void randomize(Random &rng)
        {
            for (auto &r: cpu->m_gpr)
                r = value(rng);
            cpu->m_cr.raw = rng();
            cpu->m_xer.raw = rng() & 7;
            cpu->m_lr = rng() & 0x00FFFFFC;
            cpu->m_ctr = value(rng);
            for (auto &q: cpu->m_gqr)
                q = rng();
        }

        // Doubles that exercise rounding, NaN propagation and conversion limits.
        static std::uint64_t floatBits(Random &rng)
        {
            static constexpr std::uint64_t kSpecial[] = {
                    0x0000000000000000ull, 0x8000000000000000ull, 0x3FF0000000000000ull, 0xBFF0000000000000ull, 0x7FF0000000000000ull,
                    0xFFF0000000000000ull, 0x7FF8000000000000ull, 0xFFF8000000000123ull, 0x7FF0000000000001ull, 0x7FF4000000000000ull,
                    0x0000000000000001ull, 0x800FFFFFFFFFFFFFull, 0x47EFFFFFE0000000ull, 0x47EFFFFFF0000000ull, 0x36A0000000000000ull,
                    0x41DFFFFFFFC00000ull, 0x41DFFFFFFFE00000ull, 0xC1E0000000000000ull, 0xC1E0000000100000ull, 0x41E0000000000000ull,
                    0x3FE0000000000000ull, 0x3FF8000000000000ull, 0x4004000000000000ull, 0x3810000000000000ull,
            };
            switch (rng() & 3) {
                case 0:
                    return kSpecial[rng() % std::size(kSpecial)];
                case 1: { // a value representable in single precision
                    const float f = static_cast<float>(static_cast<std::int32_t>(rng())) / static_cast<float>(1u << (rng() & 15));
                    return std::bit_cast<std::uint64_t>(static_cast<double>(f));
                }
                default:
                    return static_cast<std::uint64_t>(rng()) << 32 | rng();
            }
        }

        void randomizeFloat(Random &rng)
        {
            for (auto &f: cpu->m_fpr)
                f = std::bit_cast<double>(floatBits(rng));
            for (auto &f: cpu->m_ps1)
                f = std::bit_cast<double>(floatBits(rng));
            cpu->m_fpscr.raw = rng();
        }

        static std::uint32_t reg(Random &rng) { return rng() & 31; }
        static std::uint32_t dForm(const unsigned op, const unsigned rt, const unsigned ra, const std::uint32_t imm)
        {
            return op << 26 | rt << 21 | ra << 16 | (imm & 0xFFFF);
        }
        static std::uint32_t xForm(const unsigned xo, const unsigned rt, const unsigned ra, const unsigned rb, const bool rc = false,
                                   const bool oe = false)
        {
            return 31u << 26 | rt << 21 | ra << 16 | rb << 11 | (oe ? 1u << 10 : 0) | xo << 1 | (rc ? 1 : 0);
        }
};

#endif
