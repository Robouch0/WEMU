/*
** EPITECH PROJECT, 2025
** core
** File description:
** coreinit — OS/MEM heap allocation HLE
*/

#pragma once

#include <cstdint>

#include "cpu/interpreter/Interpreter.hpp"

// ─── OS memory ────────────────────────────────────────────────────────────────

// BOOL OSGetForegroundBucket(void **outBuffer, uint32_t *outSize)
// r3 = virtual address of a void* slot (receives the bucket base address)
// r4 = virtual address of a uint32_t slot (receives the bucket size)
// Returns TRUE (1). Allocates a 64 MB region on first call.
inline std::uint32_t OSGetForegroundBucket(Core::Interpreter &cpu,
                                            std::uint32_t outBufAddr,
                                            std::uint32_t outSizeAddr)
{
    static std::uint32_t s_bucket = 0u;
    static constexpr std::uint32_t kSize = 0x4000000u; // 64 MB

    if (!s_bucket)
        s_bucket = cpu.m_binary.m_memory.heapAlloc(kSize, 0x40000);

    cpu.m_binary.m_memory.write32(outBufAddr,  s_bucket);
    cpu.m_binary.m_memory.write32(outSizeAddr, kSize);
    return 1u;
}

// void *OSAllocFromSystem(uint32_t size, int32_t align)
inline std::uint32_t OSAllocFromSystem(Core::Interpreter &cpu,
                                        std::uint32_t size,
                                        std::int32_t  align)
{
    return cpu.m_binary.m_memory.heapAlloc(size, align);
}

// void OSFreeToSystem(void *ptr)  — bump allocator does not support freeing
inline void OSFreeToSystem(Core::Interpreter & /*cpu*/, std::uint32_t /*vaddr*/) {}

// ─── MEM expanded-heap API ────────────────────────────────────────────────────
// Heap handles are virtual addresses used as opaque tokens.
// All allocation is backed by the same bump allocator in Core::Memory.

// MEMHeapHandle MEMGetBaseHeapHandle(MEMBaseHeapType type)
// type 0 = MEM1 (foreground), type 1 = MEM2 (main / background)
inline std::uint32_t MEMGetBaseHeapHandle(Core::Interpreter &cpu, std::uint32_t type)
{
    static std::uint32_t s_handles[2] = {0u, 0u};
    const std::uint32_t  idx = type & 1u;
    if (!s_handles[idx])
        s_handles[idx] = cpu.m_binary.m_memory.heapAlloc(4u, 4u);
    return s_handles[idx];
}

// MEMHeapHandle MEMCreateExpHeapEx(void *addr, uint32_t size, uint16_t flags)
// Returns addr itself as the heap handle (decaf-emu convention).
inline std::uint32_t MEMCreateExpHeapEx(Core::Interpreter & /*cpu*/,
                                         std::uint32_t addr,
                                         std::uint32_t /*size*/,
                                         std::uint32_t /*flags*/)
{
    return addr;
}

// void *MEMAllocFromExpHeapEx(MEMHeapHandle heap, uint32_t size, int32_t align)
inline std::uint32_t MEMAllocFromExpHeapEx(Core::Interpreter &cpu,
                                            std::uint32_t /*heap*/,
                                            std::uint32_t size,
                                            std::int32_t  align)
{
    return cpu.m_binary.m_memory.heapAlloc(size, align);
}

// void MEMFreeToExpHeap(MEMHeapHandle heap, void *ptr)
inline void MEMFreeToExpHeap(Core::Interpreter & /*cpu*/,
                               std::uint32_t /*heap*/,
                               std::uint32_t /*ptr*/) {}

// void *MEMAllocFromDefaultHeap(uint32_t size)
inline std::uint32_t MEMAllocFromDefaultHeap(Core::Interpreter &cpu,
                                              std::uint32_t size)
{
    return cpu.m_binary.m_memory.heapAlloc(size, 4u);
}

// void *MEMAllocFromDefaultHeapEx(uint32_t size, int32_t align)
inline std::uint32_t MEMAllocFromDefaultHeapEx(Core::Interpreter &cpu,
                                                std::uint32_t size,
                                                std::int32_t  align)
{
    return cpu.m_binary.m_memory.heapAlloc(size, align);
}

// void MEMFreeToDefaultHeap(void *ptr)
inline void MEMFreeToDefaultHeap(Core::Interpreter & /*cpu*/,
                                   std::uint32_t /*ptr*/) {}
