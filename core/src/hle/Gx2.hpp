#pragma once

#include <cstdint>

namespace Core {
    class Interpreter;
    namespace Gx2 {
        inline constexpr std::uint32_t bufferAllocatorReturn = 0x0FFFFFE0u;
        void onBufferAllocatorReturn(Interpreter &cpu);
    } // namespace Gx2
} // namespace Core

// Registers GX2 HLE queries, command capture and display-list recording/replay.
void RegisterGx2Functions();

// End a host session before its renderer/memory and driver TLS are destroyed.
void ReleaseGx2HostResources() noexcept;

// Preserve captured command state when an HLE memory operation copies an entire
// recorded list. A write starting at a known buffer invalidates its old recording.
void CopyGx2DisplayListState(std::uint32_t destination, std::uint32_t source, std::uint32_t bytes);
