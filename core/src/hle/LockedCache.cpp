#include "hle/LockedCache.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"

namespace {
    using Core::Interpreter;
    using Core::LockedCacheState;
    using Core::Memory;
    auto &state(Interpreter &cpu) { return cpu.m_lockedCache.cores[cpu.m_scheduler.currentCoreId()]; }
    std::uint32_t base(Interpreter &cpu) { return Memory::LOCKED_CACHE_BASES[cpu.m_scheduler.currentCoreId()]; }
    std::uint32_t mask(unsigned count) { return count == 32 ? UINT32_MAX : (1u << count) - 1u; }

    void allocate(Interpreter &cpu)
    {
        const auto bytes = cpu.m_gpr[3];
        cpu.m_gpr[3] = 0;
        if (!bytes || bytes > Memory::LOCKED_CACHE_SIZE)
            return;
        const auto count = (bytes + LockedCacheState::allocationUnit - 1) / LockedCacheState::allocationUnit;
        auto &cache = state(cpu);
        for (unsigned start = 0; start <= 32 - count; ++start) {
            const auto bits = mask(count) << start;
            if (!(cache.occupied & bits)) {
                cache.occupied |= bits;
                cache.allocationLengths[start] = count;
                cpu.m_gpr[3] = base(cpu) + start * LockedCacheState::allocationUnit;
                return;
            }
        }
    }

    void deallocate(Interpreter &cpu)
    {
        const auto address = cpu.m_gpr[3];
        cpu.m_gpr[3] = 0;
        const auto offset = address - base(cpu);
        if (offset >= Memory::LOCKED_CACHE_SIZE || offset % LockedCacheState::allocationUnit)
            return;
        auto &cache = state(cpu);
        const auto start = offset / LockedCacheState::allocationUnit;
        const auto count = cache.allocationLengths[start];
        if (count) {
            cache.occupied &= ~(mask(count) << start);
            cache.allocationLengths[start] = 0;
        }
    }

    void allocatable(Interpreter &cpu)
    {
        const auto occupied = state(cpu).occupied;
        unsigned largest = 0, current = 0;
        for (unsigned i = 0; i < 32; ++i) {
            current = occupied & (1u << i) ? 0 : current + 1;
            largest = std::max(largest, current);
        }
        cpu.m_gpr[3] = largest * LockedCacheState::allocationUnit;
    }

    void enableDma(Interpreter &cpu)
    {
        const auto *thread = cpu.m_scheduler.current();
        const auto core = cpu.m_scheduler.currentCoreId();
        // Hardware DMA requires a thread pinned to its core's locked cache.
        const auto affinity = thread ? thread->affinity & 7u : 2u;
        auto &cache = state(cpu);
        cpu.m_gpr[3] = 0;
        if (affinity == (1u << core) && cache.dmaReferences != std::numeric_limits<std::uint32_t>::max()) {
            ++cache.dmaReferences;
            cpu.m_gpr[3] = 1;
        }
    }

    void transfer(Interpreter &cpu)
    {
        // The API counts 32-byte DMA blocks; zero encodes 128 blocks.
        const std::uint64_t blocks = cpu.m_gpr[5] ? cpu.m_gpr[5] : 128u;
        const auto bytes = blocks * 32;
        auto *destination = cpu.m_memory.hostPtr(cpu.m_gpr[3], bytes);
        const auto *source = cpu.m_memory.hostPtr(cpu.m_gpr[4], bytes);
        // Check both complete ranges before changing anything. Guest faults use
        // the ordinary interpreter policy, never a host out-of-bounds copy.
        if (!destination || !source)
            throw Core::MemoryException(
                    std::format("Unmapped locked-cache DMA: dst=0x{:08X} src=0x{:08X} bytes={}", cpu.m_gpr[3], cpu.m_gpr[4], bytes));
        std::memmove(destination, source, bytes);
        // Complete real byte transfers synchronously on the emulation thread.
        // The queue is consequently empty on return and LCWaitDMAQueue needs no wait.
        cpu.m_gpr[3] = 0;
    }
} // namespace

void RegisterLockedCacheFunctions()
{
    auto &syscalls = Core::syscallHandler;
    syscalls.registerSyscall("LCHardwareIsAvailable", [](Interpreter &cpu) { cpu.m_gpr[3] = 1; });
    syscalls.registerSyscall("LCGetMaxSize", [](Interpreter &cpu) { cpu.m_gpr[3] = Memory::LOCKED_CACHE_SIZE; });
    syscalls.registerSyscall("LCGetUnallocated", [](Interpreter &cpu) {
        cpu.m_gpr[3] = Memory::LOCKED_CACHE_SIZE - std::popcount(state(cpu).occupied) * LockedCacheState::allocationUnit;
    });
    syscalls.registerSyscall("LCGetAllocatableSize", allocatable);
    syscalls.registerSyscall("LCAlloc", allocate);
    syscalls.registerSyscall("LCDealloc", deallocate);
    syscalls.registerSyscall("LCEnableDMA", enableDma);
    syscalls.registerSyscall("LCDisableDMA", [](Interpreter &cpu) {
        auto &cache = state(cpu);
        if (cache.dmaReferences)
            --cache.dmaReferences;
        cpu.m_gpr[3] = 0;
    });
    syscalls.registerSyscall("LCIsDMAEnabled", [](Interpreter &cpu) { cpu.m_gpr[3] = state(cpu).dmaReferences ? 1 : 0; });
    syscalls.registerSyscall("LCLoadDMABlocks", transfer);
    syscalls.registerSyscall("LCStoreDMABlocks", transfer);
    syscalls.registerSyscall("LCWaitDMAQueue", [](Interpreter &cpu) { cpu.m_gpr[3] = 0; });
    syscalls.registerSyscall("LCGetDMAQueueLength", [](Interpreter &cpu) { cpu.m_gpr[3] = 0; });
}
