#pragma once

#include <array>
#include <cstdint>

namespace Core {
    class Memory;
}
namespace Core::Audio {
    // Offsets use AX address units: ADPCM nibbles, PCM16 words, PCM8 bytes.
    struct Source {
            std::uint16_t format{}, loopFlag{};
            std::uint32_t base{}, state{}, loopCount{}, type{};
            std::uint64_t loop{}, end{}, current{};
            std::array<std::int16_t, 16> coefficients{};
            std::uint16_t predScale{}, loopPredScale{};
            std::int16_t history1{}, history2{}, loopHistory1{}, loopHistory2{};
            float sample(const Memory &memory) const;
            void advance(const Memory &memory);
    };
    struct Voice : Source {
            std::uint32_t srcType{1};
            double ratio{1}, fraction{};
            std::int32_t envelope{}, envelopeDelta{};
            struct Gain {
                    std::int32_t volume{}, delta{};
            };
            std::array<std::array<std::array<std::array<Gain, 4>, 6>, 2>, 2> gains{};
            float render(const Memory &memory);
    };
    std::uint64_t addressUnits(std::uint32_t address, std::uint16_t format);
} // namespace Core::Audio
