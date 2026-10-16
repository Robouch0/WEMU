#pragma once

#include <cstdint>

// Registers GX2 HLE queries, command capture and display-list recording/replay.
void RegisterGx2Functions();

// End a host session before its renderer/memory and driver TLS are destroyed.
void ReleaseGx2HostResources() noexcept;

// Preserve captured command state when an HLE memory operation copies an entire
// recorded list. A write starting at a known buffer invalidates its old recording.
void CopyGx2DisplayListState(std::uint32_t destination, std::uint32_t source, std::uint32_t bytes);
