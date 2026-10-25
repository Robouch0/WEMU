#pragma once

#include <chrono>
#include <cstdint>

namespace Core::Timing {
    inline constexpr std::uint64_t timeBaseHz = 248625000ull / 4;
    inline constexpr unsigned displayHz = 60;

    // GX2 interval counts display refreshes, not rendered frames. Zero disables
    // the swap wait; a 30 Hz title requests two refreshes per frame.
    constexpr std::uint64_t swapTicks(unsigned interval) { return timeBaseHz * interval / displayHz; }
    constexpr std::chrono::duration<double> swapDuration(unsigned interval)
    {
        return std::chrono::duration<double>(static_cast<double>(interval) / displayHz);
    }
} // namespace Core::Timing
