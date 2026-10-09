#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Core::Gfx {

struct TextureSampler {
    std::array<std::uint32_t, 3> regs{};
    std::array<float, 4> customBorder{};

    [[nodiscard]] std::array<float, 4> border() const
    {
        switch ((regs[0] >> 22) & 3) {
            case 1: return {0, 0, 0, 1};
            case 2: return {1, 1, 1, 1};
            case 3: return customBorder;
            default: return {};
        }
    }

    // Base-level 2D sampling. The caller supplies already format-decoded texels.
    // Implicit derivatives, mip selection, anisotropy and comparison are not modeled.
    // Uses the magnification filter; unsupported bicubic modes currently fall back to point.
    template<class Fetch>
    std::array<float, 4> sample(std::uint32_t width, std::uint32_t height, float u, float v, Fetch &&fetch) const
    {
        return footprint<false>(width, height, u, v, fetch);
    }

    // Base-level gather of the mapped red component, in textureGather order.
    // Filter weights and min/mag filter selection do not affect gather results.
    template<class Fetch>
    std::array<float, 4> gather(std::uint32_t width, std::uint32_t height, float u, float v, Fetch &&fetch) const
    {
        return footprint<true>(width, height, u, v, fetch);
    }

private:
    template<bool Gather, class Fetch>
    std::array<float, 4> footprint(std::uint32_t width, std::uint32_t height, float u, float v, Fetch &&fetch) const
    {
        const auto outside = [&] {
            const auto value = border();
            if constexpr (Gather) return std::array<float, 4>{value[0], value[0], value[0], value[0]};
            else return value;
        };
        if (!width || !height || !std::isfinite(u) || !std::isfinite(v))
            return outside();
        const unsigned modeX = regs[0] & 7, modeY = (regs[0] >> 3) & 7;
        auto coordinate = [](double value, unsigned mode) {
            if (mode == 0)
                return value - std::floor(value);
            if (mode == 1) {
                value = std::fmod(value, 2.0);
                if (value < 0) value += 2;
                if constexpr (Gather) return value;
                return value > 1 ? 2 - value : value;
            }
            if (mode & 1) value = std::abs(value);
            return mode < 6 ? std::clamp(value, 0.0, 1.0) : value;
        };
        auto index = [](std::int64_t value, std::uint32_t size, unsigned mode) -> std::int64_t {
            if constexpr (Gather) {
                // Reflect each tap, not the coordinate: gather preserves tap order.
                if (mode == 1) {
                    const auto period = std::int64_t(size) * 2;
                    value %= period;
                    if (value < 0) value += period;
                    return value >= size ? period - 1 - value : value;
                }
            }
            if (mode == 0) {
                value %= size;
                return value < 0 ? value + size : value;
            }
            if (mode <= 3)
                return std::clamp<std::int64_t>(value, 0, size - 1);
            return value < 0 || value >= size ? -1 : value;
        };
        const double x = coordinate(u, modeX) * width;
        const double y = coordinate(v, modeY) * height;
        // Border-only coordinates can be arbitrarily large; never cast them to an integer.
        if ((!(Gather && modeX == 1) && (x < -1 || x > double(width) + 1)) ||
            (!(Gather && modeY == 1) && (y < -1 || y > double(height) + 1)))
            return outside();
        auto texel = [&](std::int64_t tx, std::int64_t ty) {
            tx = index(tx, width, modeX);
            ty = index(ty, height, modeY);
            return tx < 0 || ty < 0 ? border() : fetch(static_cast<unsigned>(tx), static_cast<unsigned>(ty));
        };
        if constexpr (!Gather) {
            if (((regs[0] >> 9) & 7) != 1)
                return texel(static_cast<std::int64_t>(std::floor(x)), static_cast<std::int64_t>(std::floor(y)));
        }
        const auto ix = static_cast<std::int64_t>(std::floor(x - 0.5));
        const auto iy = static_cast<std::int64_t>(std::floor(y - 0.5));
        const float fx = static_cast<float>(x - 0.5 - ix), fy = static_cast<float>(y - 0.5 - iy);
        const auto a = texel(ix, iy), b = texel(ix + 1, iy), c = texel(ix, iy + 1), d = texel(ix + 1, iy + 1);
        if constexpr (Gather) return {c[0], d[0], b[0], a[0]};
        std::array<float, 4> result{};
        for (unsigned channel = 0; channel < 4; channel++)
            result[channel] = std::lerp(std::lerp(a[channel], b[channel], fx), std::lerp(c[channel], d[channel], fx), fy);
        return result;
    }
};

} // namespace Core::Gfx
