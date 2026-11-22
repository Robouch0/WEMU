#pragma once

#include <array>
#include <cstdint>

namespace Core {
    class Interpreter;
}

namespace Core::Hle {
    inline constexpr std::uint32_t preinitReturn = 0x0FFFFFE4u;

    // The loader calls the optional RPX export before C/C++ constructors and entry.
    struct ApplicationBoot {
            bool active{false};
            std::uint32_t entry{}, lr{}, heapRefs{};
            std::array<std::uint32_t, 32> registers{};

            void begin(Interpreter &cpu);
            void finish(Interpreter &cpu);
    };
} // namespace Core::Hle
