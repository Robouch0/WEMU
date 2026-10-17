#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Core::Gfx::Latte {
    // Direct VTX_FETCH from a GX2 uniform-buffer descriptor (IDs 128..143).
    // Const fields supply float4 storage with a 16-byte stride. Other resource
    // types, relative operands, format overrides and fetch sizes need separate support.
    struct UniformBufferFetch {
            unsigned bank{}, source{}, channel{}, destination{}, offset{};
            std::array<unsigned, 4> selectors{};
    };
    inline std::optional<UniformBufferFetch> decodeUniformBufferFetch(std::uint32_t w0, std::uint32_t w1, std::uint32_t w2)
    {
        constexpr unsigned allowed0 = (3u << 5) | (255u << 8) | (127u << 16) | (3u << 24) | (63u << 26);
        constexpr unsigned allowed1 = 127u | (0xFFFu << 9) | (1u << 21);
        const auto buffer = (w0 >> 8) & 255;
        if ((w0 & ~allowed0) || ((w0 >> 5) & 3) != 2 || buffer < 128 || buffer >= 144 || (w0 >> 26) != 15 || (w1 & ~allowed1) || !(w1 & (1u << 21)) ||
            (w2 & ~0x8FFFFu) || !(w2 & (1u << 19)) || (w2 & 15))
            return {};
        UniformBufferFetch result{buffer - 128, (w0 >> 16) & 127, (w0 >> 24) & 3, w1 & 127, (w2 & 65535) / 16};
        for (unsigned c = 0; c < 4; ++c) {
            result.selectors[c] = (w1 >> (9 + c * 3)) & 7;
            if (result.selectors[c] == 6)
                return {};
        }
        return result;
    }
} // namespace Core::Gfx::Latte
