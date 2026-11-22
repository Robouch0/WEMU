#pragma once

#include <array>
#include <cstdint>

namespace Core {
    // Allocation/DMA bookkeeping belongs to the emulated process, not host globals.
    // Guest threads share the cache of the core selected by Scheduler::currentCoreId().
    struct LockedCacheState {
            static constexpr std::uint32_t allocationUnit = 512;
            struct CoreState {
                    std::array<std::uint8_t, 32> allocationLengths{};
                    std::uint32_t occupied{};
                    std::uint32_t dmaReferences{};
            };
            std::array<CoreState, 3> cores{};
    };
} // namespace Core

void RegisterLockedCacheFunctions();
