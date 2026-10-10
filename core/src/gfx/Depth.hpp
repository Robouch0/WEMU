#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Core::Gfx {
    enum class DepthFormat { Float32, UNorm16, UNorm24 };

    inline unsigned depthElementBytes(DepthFormat format) { return format == DepthFormat::UNorm16 ? 2 : 4; }

    inline float clampDepth(float value) { return std::isnan(value) ? 0.f : std::clamp(value, 0.f, 1.f); }

    inline float quantizeDepth(float value, DepthFormat format)
    {
        value = clampDepth(value);
        if (format == DepthFormat::Float32)
            return value;
        const double maximum = format == DepthFormat::UNorm16 ? 65535.0 : 16777215.0;
        return float(std::floor(double(value) * maximum + .5) / maximum);
    }

    inline std::uint32_t packDepth(float value, DepthFormat format)
    {
        if (format == DepthFormat::Float32)
            return std::bit_cast<std::uint32_t>(value);
        const double maximum = format == DepthFormat::UNorm16 ? 65535.0 : 16777215.0;
        return std::uint32_t(std::floor(double(clampDepth(value)) * maximum + .5));
    }

    inline float unpackDepth(std::uint32_t word, DepthFormat format)
    {
        if (format == DepthFormat::Float32)
            return std::bit_cast<float>(word);
        return format == DepthFormat::UNorm16 ? float(word & 0xFFFF) / 65535.f : float(word & 0xFFFFFF) / 16777215.f;
    }

    inline bool depthCompare(float incoming, float stored, unsigned function)
    {
        switch (function) {
            case 0:
                return false;
            case 1:
                return incoming < stored;
            case 2:
                return incoming == stored;
            case 3:
                return incoming <= stored;
            case 4:
                return incoming > stored;
            case 5:
                return incoming != stored;
            case 6:
                return incoming >= stored;
            case 7:
                return true;
            default:
                return false;
        }
    }
} // namespace Core::Gfx
