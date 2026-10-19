/*
** EPITECH PROJECT, 2026
** core
** File description:
** X64Jit -- PowerPC to x86-64 block recompiler
*/

#include "X64Jit.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <xbyak/xbyak_util.h>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/jit/Callouts.hpp"
#include "cpu/jit/CodeBuffer.hpp"
#include "cpu/jit/Emitter.hpp"
#include "cpu/types/Instruction.hpp"
#include "utils/Logger.hpp"

namespace Core::Jit {
    namespace {
        constexpr std::size_t kCodeBytes = std::size_t{256} << 20;
        // Upper bound of one block's code (at most 64 instructions); a compile needs this much room.
        constexpr std::size_t kBlockReserve = 64 * 1024;
        constexpr std::size_t kStubReserve = 4096;
        constexpr std::size_t kBlockSlots = std::size_t{1} << 18;
        constexpr std::size_t kTableBytes = sizeof(TableEntry) << kTableBits;
        constexpr std::size_t kDataBytes = kTableBytes + kBlockSlots * sizeof(BlockSlot);
        constexpr std::size_t kInstructionCount = std::size(INSTRUCTIONARRAY);

        constexpr std::size_t slotOf(const std::uint32_t pc) { return (pc >> 2) & kTableMask; }

        // WEMU_JIT_CALLOUT=integer,load,...: families kept on call-outs, to bisect a recompiler bug.
        std::uint32_t nativeFamilies()
        {
            const char *env = std::getenv("WEMU_JIT_CALLOUT");
            if (!env)
                return ~0u;
            const std::string list = env;
            if (list == "all")
                return 0;
            std::uint32_t mask = ~0u;
            const std::pair<const char *, Family> names[] = {{"integer", FamilyInteger}, {"compare", FamilyCompare}, {"load", FamilyLoad},
                                                             {"store", FamilyStore},     {"branch", FamilyBranch},   {"system", FamilySystem},
                                                             {"float", FamilyFloat},     {"paired", FamilyPaired}};
            for (const auto &[name, family]: names)
                if (list.find(name) != std::string::npos)
                    mask &= ~family;
            return mask;
        }
    } // namespace

    struct X64Jit::Impl {
            // A direct jump from some block, and where it goes while its target is not compiled.
            struct Site {
                    std::uint8_t *jump;
                    const std::uint8_t *fallback;
            };
            struct Block {
                    std::vector<std::uint32_t> memoryWords; // guest byte order, compared with RAM
                    std::vector<InstructionID> ids;
                    const std::uint8_t *code{};
                    BlockSlot *slot{};
                    bool stores{};
                    std::vector<Site> incoming; // jumps linked to this block
            };

            Impl(Interpreter &interpreter, const bool profiling) :
                cpu(interpreter), buffer(kCodeBytes, kDataBytes, reinterpret_cast<const void *>(&takePendingException)), profile(profiling)
            {
                // WEMU_JIT_PERF_MAP=1: name compiled blocks for perf (/tmp/perf-<pid>.map).
                if (const char *env = std::getenv("WEMU_JIT_PERF_MAP"); env && env[0] == '1')
                    perfMap = std::fopen(std::format("/tmp/perf-{}.map", getpid()).c_str(), "w");
            }

            ~Impl()
            {
                if (perfMap)
                    std::fclose(perfMap);
            }

            void name(const void *code, const std::size_t size, const std::string &label) const
            {
                if (!perfMap)
                    return;
                std::fprintf(perfMap, "%zx %zx %s\n", reinterpret_cast<std::size_t>(code), size, label.c_str());
                std::fflush(perfMap);
            }

            Interpreter &cpu;
            CodeBuffer buffer;
            bool profile;
            std::FILE *perfMap{};
            Context context{};
            Stubs stubs{};
            TableEntry *table{};
            BlockSlot *slots{};
            std::size_t slotCount{};
            std::uint8_t *codeStart{};
            std::uint8_t *cursor{};
            GuestLayout layout{};
            std::uint64_t seenEpoch{~std::uint64_t{0}};
            std::size_t seenHooks{~std::size_t{0}};
            std::unordered_map<std::uint32_t, Block> blocks;
            std::unordered_map<std::uint32_t, std::vector<Site>> unlinked; // target pc -> jumps waiting for it
            Stats stats{}, reported{};

            [[nodiscard]] std::int32_t offset(const void *field) const
            {
                return static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(field) - reinterpret_cast<std::intptr_t>(context.gprBase));
            }

            bool init()
            {
                if (!buffer.valid())
                    return false;
                table = reinterpret_cast<TableEntry *>(buffer.data());
                slots = reinterpret_cast<BlockSlot *>(buffer.data() + kTableBytes);
                context.gprBase = reinterpret_cast<std::uint8_t *>(&cpu.m_gpr[0]);
                const auto &ram = cpu.m_memory.getMemory();
                context.ramBias = reinterpret_cast<std::uint8_t *>(reinterpret_cast<std::uintptr_t>(ram.data()) - Memory::ApplicationCode);
                context.stackHost = cpu.m_memory.hostPtr(Memory::STACK_BASE, Memory::STACK_SIZE);
                context.slowPages = cpu.m_memory.slowPages();
                layout.cpu = offset(&cpu);
                layout.pc = offset(&cpu.m_pc);
                layout.nextPc = offset(&cpu.m_nextPc);
                layout.cr = offset(&cpu.m_cr);
                layout.xer = offset(&cpu.m_xer);
                layout.lr = offset(&cpu.m_lr);
                layout.ctr = offset(&cpu.m_ctr);
                layout.fpr = offset(&cpu.m_fpr[0]);
                layout.ps1 = offset(&cpu.m_ps1[0]);
                layout.gqr = offset(&cpu.m_gqr[0]);
                layout.fpscr = offset(&cpu.m_fpscr);
                layout.reserveValid = offset(&cpu.m_reserveValid);
                layout.reserveAddr = offset(&cpu.m_reserveAddr);
                layout.ramBytes = static_cast<std::uint32_t>(ram.size());
                layout.table = table;
                layout.memory = &cpu.m_memory;
                layout.profile = profile;
                layout.nativeMask = nativeFamilies();
                if (!buffer.writable(buffer.code(), kStubReserve))
                    return false;
                {
                    Emitter e(buffer.code(), kStubReserve, layout, stubs);
                    e.emitStubs(stubs, table);
                    codeStart = cursor = buffer.code() + ((e.getSize() + 63) & ~std::size_t{63});
                    name(buffer.code(), e.getSize(), "wemu_jit_stubs");
                }
                if (!buffer.executable(buffer.code(), kStubReserve))
                    return false;
                resetTable();
                return true;
            }

            void resetTable()
            {
                for (std::size_t i = 0; i <= kTableMask; ++i)
                    table[i] = {kEmptyPc, 0, stubs.miss};
            }

            void install(const std::uint32_t pc, const void *code) { table[slotOf(pc)] = {pc, 0, code}; }

            // Points E9 rel32 jumps elsewhere, making each touched code page writable once.
            void patch(std::vector<std::pair<std::uint8_t *, const void *>> &jumps)
            {
                std::ranges::sort(jumps);
                for (std::size_t first = 0; first < jumps.size();) {
                    std::size_t last = first;
                    while (last + 1 < jumps.size() && jumps[last + 1].first - jumps[first].first < 4096)
                        ++last;
                    const std::size_t span = static_cast<std::size_t>(jumps[last].first - jumps[first].first) + 5;
                    buffer.writable(jumps[first].first, span);
                    for (std::size_t j = first; j <= last; ++j) {
                        const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(jumps[j].second) -
                                                                   reinterpret_cast<std::intptr_t>(jumps[j].first + 5));
                        std::memcpy(jumps[j].first + 1, &rel, sizeof(rel));
                    }
                    buffer.executable(jumps[first].first, span);
                    first = last + 1;
                }
                stats.links += jumps.size();
            }

            void flush()
            {
                blocks.clear();
                unlinked.clear();
                slotCount = 0;
                cursor = codeStart;
                resetTable();
                ++stats.flushes;
            }
    };

    X64Jit::X64Jit(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

    X64Jit::~X64Jit() = default;

    std::unique_ptr<X64Jit> X64Jit::create(Interpreter &cpu, const bool profile)
    {
        using Xbyak::util::Cpu;
        const Cpu host;
        if (!host.has(Cpu::tSSE41) || !host.has(Cpu::tAVX2) || !host.has(Cpu::tFMA) || !host.has(Cpu::tBMI2) || !host.has(Cpu::tLZCNT) ||
            !host.has(Cpu::tMOVBE))
            return nullptr;
        auto impl = std::make_unique<Impl>(cpu, profile);
        if (!impl->init())
            return nullptr;
        return std::unique_ptr<X64Jit>(new X64Jit(std::move(impl)));
    }

    void X64Jit::setSpinThreshold(const std::uint64_t instructions) noexcept { m_impl->context.spinThreshold = instructions; }

    void X64Jit::sync(const std::uint64_t codeEpoch, const std::size_t hookCount)
    {
        Impl &d = *m_impl;
        // A hook added inside a compiled block must split it, as it splits decoded blocks.
        if (hookCount != d.seenHooks) {
            if (d.seenHooks != ~std::size_t{0})
                d.flush();
            d.seenHooks = hookCount;
        }
        if (codeEpoch != d.seenEpoch) {
            d.seenEpoch = codeEpoch;
            ++d.context.generation;
            ++d.stats.generations;
        }
    }

    bool X64Jit::prepare(const std::uint32_t pc)
    {
        Impl &d = *m_impl;
        if (d.table[slotOf(pc)].pc == pc)
            return true;
        const auto it = d.blocks.find(pc);
        if (it == d.blocks.end())
            return false;
        d.install(pc, it->second.code);
        return true;
    }

    void X64Jit::installHook(const std::uint32_t pc) { m_impl->install(pc, m_impl->stubs.hook); }

    bool X64Jit::compile(const std::uint32_t pc, const std::span<const GuestInstruction> instrs, const bool stores)
    {
        Impl &d = *m_impl;
        if (instrs.empty() || (pc & 3))
            return false;
        const auto started = std::chrono::steady_clock::now();
        if (d.slotCount == kBlockSlots || static_cast<std::size_t>(d.buffer.code() + d.buffer.codeBytes() - d.cursor) < kBlockReserve)
            d.flush();
        BlockSlot *slot = &d.slots[d.slotCount];
        *slot = {0, d.context.generation, 0};
        if (!d.buffer.writable(d.cursor, kBlockReserve))
            return false;
        std::size_t size = 0;
        std::vector<Link> links;
        try {
            Emitter e(d.cursor, kBlockReserve, d.layout, d.stubs);
            e.emitBlock(pc, instrs, stores, slot);
            size = e.getSize();
            links = e.links();
            d.stats.instructions += instrs.size();
            d.stats.callouts += e.callouts();
        } catch (const Xbyak::Error &error) {
            Utils::Log::error("[X64JIT] cannot compile block at 0x{:08X}: {}", pc + Memory::ApplicationCode, error.what());
        }
        if (!d.buffer.executable(d.cursor, kBlockReserve) || !size)
            return false;
        Impl::Block block{{}, {}, d.cursor, slot, stores};
        block.memoryWords.reserve(instrs.size());
        block.ids.reserve(instrs.size());
        for (const auto &instr: instrs) {
            block.memoryWords.push_back(std::byteswap(instr.raw));
            block.ids.push_back(instr.id);
        }
        d.name(block.code, size, std::format("ppc_{:08x}", pc + Memory::ApplicationCode));
        d.install(pc, block.code);
        Impl::Block &stored = d.blocks.insert_or_assign(pc, std::move(block)).first->second;
        // Link this block's exits to compiled targets (itself included), and the jumps that
        // waited for it to this block.
        std::vector<std::pair<std::uint8_t *, const void *>> jumps;
        for (const Link &link: links) {
            const Impl::Site site{link.jump, link.fallback};
            if (const auto target = d.blocks.find(link.target); target != d.blocks.end()) {
                target->second.incoming.push_back(site);
                jumps.emplace_back(site.jump, target->second.code);
            } else {
                d.unlinked[link.target].push_back(site);
            }
        }
        if (const auto waiting = d.unlinked.find(pc); waiting != d.unlinked.end()) {
            for (const Impl::Site &site: waiting->second) {
                stored.incoming.push_back(site);
                jumps.emplace_back(site.jump, stored.code);
            }
            d.unlinked.erase(waiting);
        }
        d.patch(jumps);
        ++d.slotCount;
        d.cursor += (size + 15) & ~std::size_t{15};
        d.stats.codeBytes += size;
        ++d.stats.compiled;
        d.stats.compileNanoseconds +=
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
        return true;
    }

    X64Jit::Exit X64Jit::run(std::uint64_t &retired, const std::uint64_t retireLimit, std::uint64_t &lastStoreRetired)
    {
        Impl &d = *m_impl;
        Context &c = d.context;
        c.retired = retired;
        c.retireLimit = retireLimit;
        c.lastStoreRetired = lastStoreRetired;
        c.reason = ExitReason::Continue;
        d.stubs.enter(&c, d.cpu.m_pc);
        retired = c.retired;
        lastStoreRetired = c.lastStoreRetired;
        ++d.stats.entries;
        ++d.stats.exits[static_cast<std::size_t>(c.reason) & 7];
        if (c.reason == ExitReason::Rethrow)
            std::rethrow_exception(takePendingException());
        return {c.reason, c.exitPc, c.blockPc, c.index, c.blockStores != 0};
    }

    void X64Jit::revalidate(const std::uint32_t pc)
    {
        Impl &d = *m_impl;
        const auto it = d.blocks.find(pc);
        if (it == d.blocks.end())
            return;
        const auto &ram = d.cpu.m_memory.getMemory();
        const auto &words = it->second.memoryWords;
        const std::size_t bytes = words.size() * sizeof(std::uint32_t);
        if (std::size_t{pc} + bytes <= ram.size() && std::memcmp(ram.data() + pc, words.data(), bytes) == 0) {
            it->second.slot->generation = d.context.generation;
            ++d.stats.revalidated;
            return;
        }
        // Changed code: jumps linked to the block go back to the lookup until the new words are
        // compiled.
        it->second.slot->generation = 0;
        if (d.table[slotOf(pc)].pc == pc)
            d.table[slotOf(pc)] = {kEmptyPc, 0, d.stubs.miss};
        std::vector<std::pair<std::uint8_t *, const void *>> jumps;
        auto &waiting = d.unlinked[pc];
        for (const Impl::Site &site: it->second.incoming) {
            jumps.emplace_back(site.jump, site.fallback);
            waiting.push_back(site);
        }
        d.patch(jumps);
        d.blocks.erase(it);
        ++d.stats.recompiled;
    }

    void X64Jit::flush() { m_impl->flush(); }

    const X64Jit::Stats &X64Jit::stats() const noexcept { return m_impl->stats; }

    void X64Jit::report()
    {
        Impl &d = *m_impl;
        std::vector<std::uint64_t> mix(kInstructionCount, 0);
        std::uint64_t executed = 0, blockRuns = 0, hot = 0;
        for (auto &[pc, block]: d.blocks) {
            const std::uint64_t runs = std::exchange(block.slot->executions, 0);
            if (!runs)
                continue;
            ++hot;
            blockRuns += runs;
            executed += runs * block.ids.size();
            for (const auto id: block.ids)
                mix[id] += runs;
        }
        const Stats &s = d.stats;
        const Stats &r = d.reported;
        Utils::Log::error(
                "[X64JIT] instructions={} block_runs={} hot_blocks={} blocks={} compiled={} compile_ms={:.1f} code_mb={:.1f} flushes={} links={} "
                "static_callouts={:.1f}% generations={} revalidated={} recompiled={} entries={} exits: budget={} revalidate={} miss={} done={} "
                "fatal={} suffix={}",
                executed, blockRuns, hot, d.blocks.size(), s.compiled - r.compiled,
                static_cast<double>(s.compileNanoseconds - r.compileNanoseconds) / 1e6, static_cast<double>(s.codeBytes) / (1 << 20),
                s.flushes - r.flushes, s.links - r.links,
                100.0 * static_cast<double>(s.callouts) / static_cast<double>(s.instructions ? s.instructions : 1), s.generations - r.generations,
                s.revalidated - r.revalidated, s.recompiled - r.recompiled, s.entries - r.entries, s.exits[1] - r.exits[1], s.exits[2] - r.exits[2],
                s.exits[3] - r.exits[3], s.exits[4] - r.exits[4], s.exits[5] - r.exits[5], s.exits[7] - r.exits[7]);
        d.reported = s;
        if (!executed)
            return;
        std::vector<std::size_t> order(kInstructionCount);
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return mix[a] > mix[b]; });
        for (std::size_t i = 0; i < order.size() && mix[order[i]]; ++i)
            Utils::Log::error("[X64JIT_MIX] {} count={} share={:.2f}%", instructionIDToString(static_cast<InstructionID>(order[i])), mix[order[i]],
                              100.0 * static_cast<double>(mix[order[i]]) / static_cast<double>(executed));
    }
} // namespace Core::Jit
