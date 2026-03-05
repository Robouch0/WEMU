/*
** EPITECH PROJECT, 2025
** core
** File description:
** coreinit HLE — main registration header
*/

#pragma once

#include "cpu/interpreter/SyscallHandler.hpp"
#include "lib/coreinit/OsCache.hpp"
#include "lib/coreinit/OsMemory.hpp"
#include "lib/coreinit/OsScreen.hpp"
#include "lib/coreinit/OsSystem.hpp"

inline void RegisterCoreInitFunctions()
{
    // ── OSScreen ──────────────────────────────────────────────────────────────
    REGISTER_SYSCALL(OSScreenInit);
    REGISTER_SYSCALL(OSScreenGetBufferSizeEx);
    REGISTER_SYSCALL(OSScreenSetBufferEx);
    REGISTER_SYSCALL(OSScreenEnableEx);
    REGISTER_SYSCALL(OSScreenClearBuffersEx);
    REGISTER_SYSCALL(OSScreenPutFontEx);
    REGISTER_SYSCALL(OSScreenFlipBuffersEx);

    // ── OS memory ─────────────────────────────────────────────────────────────
    REGISTER_SYSCALL(OSGetForegroundBucket);
    REGISTER_SYSCALL(OSAllocFromSystem);
    REGISTER_SYSCALL(OSFreeToSystem);

    // ── MEM heap API ──────────────────────────────────────────────────────────
    REGISTER_SYSCALL(MEMGetBaseHeapHandle);
    REGISTER_SYSCALL(MEMCreateExpHeapEx);
    REGISTER_SYSCALL(MEMAllocFromExpHeapEx);
    REGISTER_SYSCALL(MEMFreeToExpHeap);
    REGISTER_SYSCALL(MEMAllocFromDefaultHeap);
    REGISTER_SYSCALL(MEMAllocFromDefaultHeapEx);
    REGISTER_SYSCALL(MEMFreeToDefaultHeap);

    // ── Cache (no-ops) ────────────────────────────────────────────────────────
    REGISTER_SYSCALL(DCFlushRange);
    REGISTER_SYSCALL(DCInvalidateRange);
    REGISTER_SYSCALL(DCStoreRange);
    REGISTER_SYSCALL(DCFlushRangeNoSync);
    REGISTER_SYSCALL(ICInvalidateRange);

    // ── System ────────────────────────────────────────────────────────────────
    RegisterOsSystemFunctions();
}
