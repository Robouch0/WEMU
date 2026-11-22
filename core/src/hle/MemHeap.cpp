/*
** EPITECH PROJECT, 2026
** core
** File description:
** MemHeap -- guest-visible Cafe MEM heaps (base heaps, ExpHeap, FrmHeap) with real free
*/

#include "MemHeap.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <unordered_map>

#include "CoreinitExtra.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/EncodedInstruction.hpp"
#include "utils/Logger.hpp"

// A MEMHeapHandle on a real Wii U is a pointer to a MEMHeapHeader living in guest memory, and
// titles (MK8's sead heap manager in particular) read its fields directly -- notably the 'EXPH'
// tag at +0x00 and the dataStart/dataEnd range at +0x20/+0x24 -- to size their own root heaps.
// So every heap we hand out writes a real header into guest memory; the allocation bookkeeping
// (free/used block maps with coalescing) stays host-side for simplicity.

namespace {

    constexpr std::uint32_t TAG_EXPH = 0x45585048; // 'EXPH'
    constexpr std::uint32_t TAG_FRMH = 0x46524D48; // 'FRMH'
    constexpr std::uint32_t HEADER_RESERVE = 0x80; // guest bytes reserved for the header

    constexpr std::uint32_t OFF_TAG = 0x00;
    constexpr std::uint32_t OFF_DATA_START = 0x20;
    constexpr std::uint32_t OFF_DATA_END = 0x24;

    // Guest arena carved for the base heaps (the internal HLE bump pool lives below at 0x26000000).
    // MK8's sead heap manager carves ~700 MB of child heaps out of MEM2 (a real Wii U title gets
    // ~1 GB), so the arena must be big enough for its largest request (0x11900000) plus siblings.
    // 1 GB MEM2: MK8's sound system alone stages ~280 MB of driver buffers on top of the sead
    // heaps, and our GX2 surface sizing pads more than real tiling does, so 896 MB ran dry
    // (nw::snd got a NULL buffer and crashed the driver ctor at 0x2427968).
    constexpr std::uint32_t MEM2_BASE = 0x28000000, MEM2_SIZE = 0x40000000; // 1 GB app arena
    constexpr std::uint32_t MEM1_BASE = 0x68000000, MEM1_SIZE = 0x02000000; // 32 MB MEM1 (hardware size; MK8 takes it all)
    constexpr std::uint32_t FG_BASE = 0x6A000000, FG_SIZE = 0x02800000; // 40 MB FG bucket (hardware size)

    struct ExpHeap {
            std::uint32_t dataStart{0};
            std::uint32_t dataEnd{0};
            std::map<std::uint32_t, std::uint32_t> free; // start -> size, coalesced, address-ordered
            std::unordered_map<std::uint32_t, std::uint32_t> used; // start -> size

            void init(std::uint32_t start, std::uint32_t end)
            {
                dataStart = start;
                dataEnd = end;
                free.clear();
                used.clear();
                free[start] = end - start;
            }

            void carve(std::uint32_t blockStart, std::uint32_t blockSize, std::uint32_t p, std::uint32_t size)
            {
                free.erase(blockStart);
                if (p > blockStart)
                    free[blockStart] = p - blockStart;
                if (blockStart + blockSize > p + size)
                    free[p + size] = blockStart + blockSize - (p + size);
                used[p] = size;
            }

            // Negative alignment is the Cafe convention for "allocate from the top of the heap".
            std::uint32_t alloc(std::uint32_t size, std::int32_t alignSigned)
            {
                const bool fromTop = alignSigned < 0;
                std::uint32_t align = fromTop ? static_cast<std::uint32_t>(-alignSigned) : static_cast<std::uint32_t>(alignSigned);
                if (align < 4)
                    align = 4;
                size = (std::max(size, 1u) + 3) & ~3u;

                if (!fromTop) {
                    for (const auto &[start, len]: free) {
                        const std::uint32_t p = (start + align - 1) & ~(align - 1);
                        if (p + size <= start + len && p + size > p) {
                            carve(start, len, p, size);
                            return p;
                        }
                    }
                } else {
                    for (auto it = free.rbegin(); it != free.rend(); ++it) {
                        const std::uint32_t end = it->first + it->second;
                        if (end < size)
                            continue;
                        const std::uint32_t p = (end - size) & ~(align - 1);
                        if (p >= it->first) {
                            carve(it->first, it->second, p, size);
                            return p;
                        }
                    }
                }
                return 0;
            }

            void release(std::uint32_t p)
            {
                const auto it = used.find(p);
                if (it == used.end())
                    return;
                const std::uint32_t size = it->second;
                used.erase(it);
                const auto [ins, ok] = free.emplace(p, size);
                if (!ok)
                    return;
                if (const auto next = std::next(ins); next != free.end() && ins->first + ins->second == next->first) {
                    ins->second += next->second;
                    free.erase(next);
                }
                if (ins != free.begin()) {
                    if (const auto prev = std::prev(ins); prev->first + prev->second == ins->first) {
                        prev->second += ins->second;
                        free.erase(ins);
                    }
                }
            }

            [[nodiscard]] std::uint32_t largestAllocatable(std::int32_t alignSigned) const
            {
                std::uint32_t align = alignSigned < 0 ? -alignSigned : alignSigned;
                if (align < 4)
                    align = 4;
                std::uint32_t best = 0;
                for (const auto &[start, len]: free) {
                    const std::uint32_t p = (start + align - 1) & ~(align - 1);
                    if (p < start + len)
                        best = std::max(best, start + len - p);
                }
                return best;
            }

            [[nodiscard]] std::uint32_t totalFree() const
            {
                std::uint32_t sum = 0;
                for (const auto &[_, len]: free)
                    sum += len;
                return sum;
            }
    };

    std::map<std::uint32_t, ExpHeap> g_heaps; // handle (= header guest address) -> heap
    std::uint32_t g_baseHeap[9] = {}; // MEMBaseHeapType: 0=MEM1, 1=MEM2, 8=FG

    std::uint32_t mem2Size()
    {
        static const std::uint32_t size = []() -> std::uint32_t {
            const char *env = std::getenv("WEMU_MEM2_SIZE");
            if (!env)
                return MEM2_SIZE;
            const std::uint32_t requested = static_cast<std::uint32_t>(std::strtoul(env, nullptr, 0));
            if (requested < 0x10000000u)
                return MEM2_SIZE;
            // Keep the guest-visible arena below MEM1 at 0x68000000. This makes the knob useful for
            // MK8 memory-pressure diagnosis without overlapping the separate MEM1/FG regions.
            return std::min<std::uint32_t>(requested, MEM1_BASE - MEM2_BASE);
        }();
        return size;
    }

    // Writes the guest-visible header and registers the host-side allocator state.
    std::uint32_t createHeap(Core::Interpreter &cpu, std::uint32_t base, std::uint32_t size, std::uint32_t tag)
    {
        if (!base || size <= HEADER_RESERVE)
            return 0;
        cpu.m_memory.write<std::uint32_t>(base + OFF_TAG, tag);
        cpu.m_memory.write<std::uint32_t>(base + OFF_DATA_START, base + HEADER_RESERVE);
        cpu.m_memory.write<std::uint32_t>(base + OFF_DATA_END, base + size);
        cpu.m_memory.write<std::uint32_t>(base + 0x40, base + HEADER_RESERVE);
        cpu.m_memory.write<std::uint32_t>(base + 0x44, base + size);
        cpu.m_memory.write<std::uint32_t>(base + 0x48, 0);
        g_heaps[base].init(base + HEADER_RESERVE, base + size);
        Utils::Log::debug("[MEM] heap 0x{:08X} [0x{:08X}..0x{:08X}]", base, base + HEADER_RESERVE, base + size);
        return base;
    }

    void ensureBaseHeaps(Core::Interpreter &cpu)
    {
        if (g_baseHeap[1])
            return;
        g_baseHeap[0] = createHeap(cpu, MEM1_BASE, MEM1_SIZE, TAG_EXPH);
        g_baseHeap[1] = createHeap(cpu, MEM2_BASE, mem2Size(), TAG_EXPH);
        g_baseHeap[8] = createHeap(cpu, FG_BASE, FG_SIZE, TAG_EXPH);
    }

    ExpHeap *findHeap(std::uint32_t handle)
    {
        const auto it = g_heaps.find(handle);
        return it == g_heaps.end() ? nullptr : &it->second;
    }

    // ---- handlers -------------------------------------------------------------------------------

    void mem_GetBaseHeapHandle(Core::Interpreter &cpu)
    {
        ensureBaseHeaps(cpu);
        const std::uint32_t type = cpu.m_gpr[3];
        cpu.m_gpr[3] = type < 9 ? g_baseHeap[type] : 0;
    }

    void mem_CreateExpHeapEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = createHeap(cpu, cpu.m_gpr[3], cpu.m_gpr[4], TAG_EXPH); }
    void mem_CreateFrmHeapEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = createHeap(cpu, cpu.m_gpr[3], cpu.m_gpr[4], TAG_FRMH); }

    void mem_DestroyHeap(Core::Interpreter &cpu)
    {
        g_heaps.erase(cpu.m_gpr[3]);
        // MEMDestroyExpHeap returns the heap's start address.
    }

    void heapAlloc(Core::Interpreter &cpu, std::uint32_t handle, std::uint32_t size, std::int32_t align)
    {
        if (ExpHeap *h = findHeap(handle)) {
            cpu.m_gpr[3] = h->alloc(size, align);
            if (!cpu.m_gpr[3])
                Utils::Log::error("[MEM] heap 0x{:08X} exhausted (size=0x{:X} align={})", handle, size, align);
            return;
        }
        Utils::Log::error("[MEM] alloc from unknown heap handle 0x{:08X}; using internal pool", handle);
        cpu.m_gpr[3] = cpu.m_memory.heapAllocate(size, align < 0 ? -align : (align ? align : 8));
    }

    void mem_AllocFromExpHeapEx(Core::Interpreter &cpu) { heapAlloc(cpu, cpu.m_gpr[3], cpu.m_gpr[4], static_cast<std::int32_t>(cpu.m_gpr[5])); }

    void mem_FreeToExpHeap(Core::Interpreter &cpu)
    {
        if (ExpHeap *h = findHeap(cpu.m_gpr[3]))
            h->release(cpu.m_gpr[4]);
        cpu.m_gpr[3] = 0;
    }

    void mem_GetAllocatableSizeForExpHeapEx(Core::Interpreter &cpu)
    {
        const ExpHeap *h = findHeap(cpu.m_gpr[3]);
        cpu.m_gpr[3] = h ? h->largestAllocatable(static_cast<std::int32_t>(cpu.m_gpr[4])) : 0;
    }

    void mem_GetTotalFreeSizeForExpHeap(Core::Interpreter &cpu)
    {
        const ExpHeap *h = findHeap(cpu.m_gpr[3]);
        cpu.m_gpr[3] = h ? h->totalFree() : 0;
    }

    // MEMGetSizeForMBlockExpHeap takes the allocation pointer, not a heap handle.
    void mem_GetSizeForMBlockExpHeap(Core::Interpreter &cpu)
    {
        const auto pointer = cpu.m_gpr[3];
        for (const auto &[handle, heap]: g_heaps)
            if (const auto block = heap.used.find(pointer); block != heap.used.end()) {
                cpu.m_gpr[3] = block->second;
                return;
            }
        cpu.m_gpr[3] = 0;
    }

    // Default heap = MEM2 base heap (matches the Cafe runtime, which routes the exported
    // MEMAllocFromDefaultHeap[Ex] pointers there with 0x40 alignment).
    void mem_AllocFromDefaultHeap(Core::Interpreter &cpu)
    {
        ensureBaseHeaps(cpu);
        heapAlloc(cpu, g_baseHeap[1], cpu.m_gpr[3], 0x40);
    }

    void mem_AllocFromDefaultHeapEx(Core::Interpreter &cpu)
    {
        ensureBaseHeaps(cpu);
        heapAlloc(cpu, g_baseHeap[1], cpu.m_gpr[3], static_cast<std::int32_t>(cpu.m_gpr[4]));
    }

    void mem_FreeToDefaultHeap(Core::Interpreter &cpu)
    {
        ensureBaseHeaps(cpu);
        if (ExpHeap *h = findHeap(g_baseHeap[1]))
            h->release(cpu.m_gpr[3]);
        cpu.m_gpr[3] = 0;
    }

    // Frame heaps have two cursors and a stack of checkpoints stored in the heap.
    // Keep the free-range bookkeeping coherent for clients creating a child heap
    // from the remaining frame-heap arena.
    void syncFrameHeap(Core::Interpreter &cpu, std::uint32_t heap)
    {
        auto *h = findHeap(heap);
        const auto head = cpu.m_memory.read<std::uint32_t>(heap + 0x40);
        const auto tail = cpu.m_memory.read<std::uint32_t>(heap + 0x44);
        h->free.clear();
        if (tail > head)
            h->free[head] = tail - head;
        std::erase_if(h->used, [&](const auto &block) { return block.first >= head && std::uint64_t(block.first) + block.second <= tail; });
    }

    void mem_AllocFromFrmHeapEx(Core::Interpreter &cpu)
    {
        const auto heap = cpu.m_gpr[3];
        auto *h = findHeap(heap);
        if (!h) {
            cpu.m_gpr[3] = 0;
            return;
        }
        const auto head = cpu.m_memory.read<std::uint32_t>(heap + 0x40);
        const auto tail = cpu.m_memory.read<std::uint32_t>(heap + 0x44);
        const auto signedAlign = static_cast<std::int32_t>(cpu.m_gpr[5]);
        const auto align = std::max(4u, signedAlign < 0 ? 0u - cpu.m_gpr[5] : cpu.m_gpr[5]);
        const auto size = (std::uint64_t(std::max(1u, cpu.m_gpr[4])) + 3) & ~std::uint64_t(3);
        const auto address = signedAlign < 0 ? (std::uint64_t(tail) - std::min(size, std::uint64_t(tail))) & ~std::uint64_t(align - 1)
                                             : (std::uint64_t(head) + align - 1) & ~std::uint64_t(align - 1);
        if ((align & (align - 1)) || address < head || address + size > tail) {
            cpu.m_gpr[3] = 0;
            return;
        }
        cpu.m_memory.write<std::uint32_t>(heap + (signedAlign < 0 ? 0x44 : 0x40), signedAlign < 0 ? address : address + size);
        h->used[address] = size;
        syncFrameHeap(cpu, heap);
        cpu.m_gpr[3] = address;
    }

    void mem_RecordStateForFrmHeap(Core::Interpreter &cpu)
    {
        const auto heap = cpu.m_gpr[3], tag = cpu.m_gpr[4];
        if (!findHeap(heap)) {
            cpu.m_gpr[3] = 0;
            return;
        }
        const auto head = cpu.m_memory.read<std::uint32_t>(heap + 0x40);
        const auto tail = cpu.m_memory.read<std::uint32_t>(heap + 0x44);
        const auto previous = cpu.m_memory.read<std::uint32_t>(heap + 0x48);
        cpu.m_gpr[4] = 16;
        cpu.m_gpr[5] = 4;
        mem_AllocFromFrmHeapEx(cpu);
        const auto state = cpu.m_gpr[3];
        if (!state)
            return;
        cpu.m_memory.write<std::uint32_t>(state, tag);
        cpu.m_memory.write<std::uint32_t>(state + 4, head);
        cpu.m_memory.write<std::uint32_t>(state + 8, tail);
        cpu.m_memory.write<std::uint32_t>(state + 12, previous);
        cpu.m_memory.write<std::uint32_t>(heap + 0x48, state);
        cpu.m_gpr[3] = 1;
    }

    void mem_FreeByStateToFrmHeap(Core::Interpreter &cpu)
    {
        const auto heap = cpu.m_gpr[3], tag = cpu.m_gpr[4];
        auto *h = findHeap(heap);
        if (!h) {
            cpu.m_gpr[3] = 0;
            return;
        }
        auto state = cpu.m_memory.read<std::uint32_t>(heap + 0x48);
        // Bound traversal by the arena size, including malformed guest chains.
        for (auto remaining = (h->dataEnd - h->dataStart) / 16; state && remaining; --remaining) {
            if (state < h->dataStart || std::uint64_t(state) + 16 > h->dataEnd)
                break;
            if (!tag || cpu.m_memory.read<std::uint32_t>(state) == tag) {
                cpu.m_memory.write<std::uint32_t>(heap + 0x40, cpu.m_memory.read<std::uint32_t>(state + 4));
                cpu.m_memory.write<std::uint32_t>(heap + 0x44, cpu.m_memory.read<std::uint32_t>(state + 8));
                cpu.m_memory.write<std::uint32_t>(heap + 0x48, cpu.m_memory.read<std::uint32_t>(state + 12));
                syncFrameHeap(cpu, heap);
                cpu.m_gpr[3] = 1;
                return;
            }
            state = cpu.m_memory.read<std::uint32_t>(state + 12);
        }
        cpu.m_gpr[3] = 0;
    }

    void mem_FreeToFrmHeap(Core::Interpreter &cpu)
    {
        const auto heap = cpu.m_gpr[3], mode = cpu.m_gpr[4];
        if (const auto *h = findHeap(heap)) {
            if (mode & 1)
                cpu.m_memory.write<std::uint32_t>(heap + 0x40, h->dataStart);
            if (mode & 2)
                cpu.m_memory.write<std::uint32_t>(heap + 0x44, h->dataEnd);
            if (mode & 3)
                cpu.m_memory.write<std::uint32_t>(heap + 0x48, 0);
            syncFrameHeap(cpu, heap);
        }
    }

    void allocatorExpAlloc(Core::Interpreter &cpu)
    {
        const auto allocator = cpu.m_gpr[3];
        heapAlloc(cpu, cpu.m_memory.read<std::uint32_t>(allocator + 4), cpu.m_gpr[4], cpu.m_memory.read<std::int32_t>(allocator + 8));
    }

    void allocatorExpFree(Core::Interpreter &cpu)
    {
        cpu.m_gpr[3] = cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3] + 4);
        mem_FreeToExpHeap(cpu);
    }

    void mem_InitAllocatorForExpHeap(Core::Interpreter &cpu)
    {
        const auto allocator = cpu.m_gpr[3];
        const auto table = cpu.m_memory.heapAllocate(8, 4);
        cpu.m_memory.write<std::uint32_t>(table, Core::Hle::exportFunction(cpu, "__wemu_allocatorExpAlloc"));
        cpu.m_memory.write<std::uint32_t>(table + 4, Core::Hle::exportFunction(cpu, "__wemu_allocatorExpFree"));
        cpu.m_memory.write<std::uint32_t>(allocator, table);
        cpu.m_memory.write<std::uint32_t>(allocator + 4, cpu.m_gpr[4]);
        cpu.m_memory.write<std::uint32_t>(allocator + 8, cpu.m_gpr[5]);
        cpu.m_gpr[3] = 0;
    }

    template<unsigned Offset>
    void allocatorDispatch(Core::Interpreter &cpu)
    {
        const auto table = cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[3]);
        cpu.m_ctr = cpu.m_memory.read<std::uint32_t>(table + Offset);
        // This API is a tail call: preserve the caller's LR and allocator/size
        // arguments. BCTR handles either a native guest function or an HLE export.
        EncodedInstruction branch(0);
        branch.rt = 20;
        Core::Instruction::BCTR(cpu, branch);
        cpu.m_hle_redirected = true;
    }

} // namespace

namespace Core::Hle {
    std::array<std::uint32_t, 3> applicationHeapHandles(Core::Interpreter &cpu)
    {
        ensureBaseHeaps(cpu);
        return {g_baseHeap[0], g_baseHeap[8], g_baseHeap[1]};
    }

    void setApplicationHeapHandles(const std::array<std::uint32_t, 3> &handles)
    {
        g_baseHeap[0] = handles[0];
        g_baseHeap[8] = handles[1];
        g_baseHeap[1] = handles[2];
    }

    std::uint32_t defaultHeapHandle(Core::Interpreter &cpu)
    {
        ensureBaseHeaps(cpu);
        return g_baseHeap[1];
    }
} // namespace Core::Hle

void RegisterMemHeapFunctions()
{
    Core::syscallHandler.registerSyscall("MEMGetBaseHeapHandle", mem_GetBaseHeapHandle);
    Core::syscallHandler.registerSyscall("MEMCreateExpHeapEx", mem_CreateExpHeapEx);
    Core::syscallHandler.registerSyscall("MEMCreateFrmHeapEx", mem_CreateFrmHeapEx);
    Core::syscallHandler.registerSyscall("MEMDestroyExpHeap", mem_DestroyHeap);
    Core::syscallHandler.registerSyscall("MEMDestroyFrmHeap", mem_DestroyHeap);
    Core::syscallHandler.registerSyscall("MEMAllocFromExpHeapEx", mem_AllocFromExpHeapEx);
    Core::syscallHandler.registerSyscall("MEMFreeToExpHeap", mem_FreeToExpHeap);
    Core::syscallHandler.registerSyscall("MEMGetAllocatableSizeForExpHeapEx", mem_GetAllocatableSizeForExpHeapEx);
    Core::syscallHandler.registerSyscall("MEMGetTotalFreeSizeForExpHeap", mem_GetTotalFreeSizeForExpHeap);
    Core::syscallHandler.registerSyscall("MEMGetSizeForMBlockExpHeap", mem_GetSizeForMBlockExpHeap);
    Core::syscallHandler.registerSyscall("MEMAllocFromDefaultHeap", mem_AllocFromDefaultHeap);
    Core::syscallHandler.registerSyscall("MEMAllocFromDefaultHeapEx", mem_AllocFromDefaultHeapEx);
    Core::syscallHandler.registerSyscall("MEMFreeToDefaultHeap", mem_FreeToDefaultHeap);
    Core::syscallHandler.registerSyscall("MEMAllocFromFrmHeapEx", mem_AllocFromFrmHeapEx);
    Core::syscallHandler.registerSyscall("MEMRecordStateForFrmHeap", mem_RecordStateForFrmHeap);
    Core::syscallHandler.registerSyscall("MEMFreeByStateToFrmHeap", mem_FreeByStateToFrmHeap);
    Core::syscallHandler.registerSyscall("MEMFreeToFrmHeap", mem_FreeToFrmHeap);
    Core::syscallHandler.registerSyscall("MEMGetAllocatableSizeForFrmHeapEx", mem_GetAllocatableSizeForExpHeapEx);
    Core::syscallHandler.registerSyscall("MEMInitAllocatorForExpHeap", mem_InitAllocatorForExpHeap);
    Core::syscallHandler.registerSyscall("MEMAllocFromAllocator", allocatorDispatch<0>);
    Core::syscallHandler.registerSyscall("MEMFreeToAllocator", allocatorDispatch<4>);
    Core::syscallHandler.registerSyscall("__wemu_allocatorExpAlloc", allocatorExpAlloc);
    Core::syscallHandler.registerSyscall("__wemu_allocatorExpFree", allocatorExpFree);
}
