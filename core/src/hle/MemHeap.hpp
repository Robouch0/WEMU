/*
** EPITECH PROJECT, 2026
** core
** File description:
** MemHeap -- guest-visible Cafe MEM heaps (base heaps, ExpHeap, FrmHeap) with real free
*/

#pragma once

#include <cstdint>

void RegisterMemHeapFunctions();

namespace Core {
    class Interpreter;
}

namespace Core::Hle {
    // Handle of the MEM2 base heap (the default heap). Created lazily on first use.
    std::uint32_t defaultHeapHandle(Core::Interpreter &cpu);
} // namespace Core::Hle
