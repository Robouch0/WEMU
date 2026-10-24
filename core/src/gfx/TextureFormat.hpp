#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace Core::Gfx {
    // Latte FMT_4_4: R occupies the low nibble, G the high nibble.
    inline std::array<std::uint8_t, 4> decodeRg4Unorm(std::uint8_t value)
    {
        return {std::uint8_t((value & 15) * 17), std::uint8_t((value >> 4) * 17), 0, 255};
    }

    inline std::array<float, 4> decodeRg16Unorm(const std::uint8_t *p)
    {
        const unsigned r = unsigned(p[0]) | (unsigned(p[1]) << 8);
        const unsigned g = unsigned(p[2]) | (unsigned(p[3]) << 8);
        return {float(r) / 65535.f, float(g) / 65535.f, 0.f, 1.f};
    }

    // Latte FMT_5_6_5 stores R in bits 0..4, G in 5..10, B in 11..15.
    // Surface words use GPU little-endian order, independent of the PPC CPU.
    inline std::array<std::uint8_t, 4> decodeRgb565(const std::uint8_t *p)
    {
        const unsigned word = unsigned(p[0]) | (unsigned(p[1]) << 8);
        const unsigned r = word & 31, g = (word >> 5) & 63, b = (word >> 11) & 31;
        return {std::uint8_t((r * 255 + 15) / 31), std::uint8_t((g * 255 + 31) / 63), std::uint8_t((b * 255 + 15) / 31), 255};
    }

    // Signed RGTC endpoints are normalized before interpolation. -128 has
    // the same -1 value as -127; selectors 6/7 in six-entry mode supply -1/+1.
    inline float decodeBc4Snorm(const std::uint8_t *block, unsigned pixel)
    {
        const int a = std::max(int(std::bit_cast<std::int8_t>(block[0])), -127);
        const int b = std::max(int(std::bit_cast<std::int8_t>(block[1])), -127);
        std::uint64_t selectors{};
        for (unsigned i = 0; i < 6; ++i)
            selectors |= std::uint64_t(block[i + 2]) << (8 * i);
        const unsigned select = (selectors >> (pixel * 3)) & 7;
        if (select == 0)
            return float(a) / 127.f;
        if (select == 1)
            return float(b) / 127.f;
        if (a > b)
            return float((8 - int(select)) * a + (int(select) - 1) * b) / (7.f * 127.f);
        if (select < 6)
            return float((6 - int(select)) * a + (int(select) - 1) * b) / (5.f * 127.f);
        return select == 6 ? -1.f : 1.f;
    }

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
