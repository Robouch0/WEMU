#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace Core::Gfx {
    enum class ColorFormat { RGBA8, RGBA32Float };
    constexpr unsigned colorElementBytes(ColorFormat format) { return format == ColorFormat::RGBA32Float ? 16u : 4u; }
    constexpr ColorFormat colorFormat(unsigned gx2Format) { return gx2Format == 0x823 ? ColorFormat::RGBA32Float : ColorFormat::RGBA8; }
    inline std::array<float, 4> readFloatColor(const std::uint8_t *bytes)
    {
        std::array<float, 4> value;
        std::memcpy(value.data(), bytes, sizeof(value));
        return value;
    }
    inline void writeFloatColor(std::uint8_t *bytes, const std::array<float, 4> &value, unsigned mask)
    {
        for (unsigned c = 0; c < 4; ++c)
            if (mask & (1u << c))
                std::memcpy(bytes + c * sizeof(float), &value[c], sizeof(float));
    }
    inline std::array<float, 4> blendFloatColor(const std::array<float, 4> &src, const std::array<float, 4> &dst, unsigned colorSource,
                                                unsigned colorDestination, unsigned colorOperation, unsigned alphaSource, unsigned alphaDestination,
                                                unsigned alphaOperation, const std::array<std::uint8_t, 4> &constant)
    {
        const auto factor = [&](unsigned mode, unsigned c) {
            switch (mode) {
                case 0:
                    return 0.f;
                case 1:
                    return 1.f;
                case 2:
                    return src[c];
                case 3:
                    return 1.f - src[c];
                case 4:
                case 11:
                    return src[3];
                case 5:
                case 12:
                    return 1.f - src[3];
                case 6:
                    return dst[3];
                case 7:
                    return 1.f - dst[3];
                case 8:
                    return dst[c];
                case 9:
                    return 1.f - dst[c];
                case 10:
                    return c == 3 ? 1.f : std::min(src[3], 1.f - dst[3]);
                case 13:
                    return float(constant[c]) / 255.f;
                case 14:
                    return 1.f - float(constant[c]) / 255.f;
                case 19:
                    return float(constant[3]) / 255.f;
                case 20:
                    return 1.f - float(constant[3]) / 255.f;
                default:
                    return 1.f;
            }
        };
        std::array<float, 4> out;
        for (unsigned c = 0; c < 4; ++c) {
            const auto op = c == 3 ? alphaOperation : colorOperation;
            const auto s = src[c] * factor(c == 3 ? alphaSource : colorSource, c);
            const auto d = dst[c] * factor(c == 3 ? alphaDestination : colorDestination, c);
            out[c] = op == 1 ? s - d : op == 2 ? std::min(src[c], dst[c]) : op == 3 ? std::max(src[c], dst[c]) : op == 4 ? d - s : s + d;
        }
        return out;
    }
} // namespace Core::Gfx
