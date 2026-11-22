/*
** EPITECH PROJECT, 2026
** core
** File description:
** MemHeap -- guest-visible Cafe MEM heaps (base heaps, ExpHeap, FrmHeap) with real free
*/

#pragma once

#include <array>
#include <cstdint>

void RegisterMemHeapFunctions();

namespace Core {
    class Interpreter;
}

namespace Core::Hle {
    // Handle of the MEM2 base heap (the default heap). Created lazily on first use.
    std::uint32_t defaultHeapHandle(Core::Interpreter &cpu);
    std::array<std::uint32_t, 3> applicationHeapHandles(Core::Interpreter &cpu);
    void setApplicationHeapHandles(const std::array<std::uint32_t, 3> &handles);
} // namespace Core::Hle
