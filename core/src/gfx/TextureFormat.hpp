#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace Core::Gfx {
    // GPU surface words are little-endian, independently of the PPC CPU byte order.
    inline std::array<float, 4> decodeR32Float(const std::uint8_t *p, std::uint32_t map)
    {
        const std::uint32_t bits = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
        const std::array<float, 4> raw{std::bit_cast<float>(bits), 0, 0, 1};
        std::array<float, 4> result{};
        for (unsigned c = 0; c < 4; ++c) {
            const auto selector = (map >> (24 - c * 8)) & 7;
            result[c] = selector < 4 ? raw[selector] : selector == 5 ? 1.0f : 0.0f;
        }
        return result;
    }
} // namespace Core::Gfx
