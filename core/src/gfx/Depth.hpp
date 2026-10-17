#pragma once

#include <algorithm>
#include <array>
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

    // Polygon offset is a triangle-constant addition to geometric window depth.
    // Float units use the largest depth exponent of the primitive; fixed-point
    // units use the destination depth resolution. Shader depth exports replace it.
    inline float polygonDepthBias(const std::array<float, 3> &x, const std::array<float, 3> &y, const std::array<float, 3> &z, DepthFormat format,
                                  float offset, float scale, float clamp)
    {
        const double dx1 = double(x[1]) - x[0], dx2 = double(x[2]) - x[0];
        const double dy1 = double(y[1]) - y[0], dy2 = double(y[2]) - y[0];
        const double area = dx1 * dy2 - dx2 * dy1;
        if (area == 0)
            return 0;
        const double dz1 = double(z[1]) - z[0], dz2 = double(z[2]) - z[0];
        const double slope = std::max(std::abs((dz1 * dy2 - dz2 * dy1) / area), std::abs((dx1 * dz2 - dx2 * dz1) / area));
        double unit;
        if (format == DepthFormat::Float32) {
            const float maximum = std::max({std::abs(z[0]), std::abs(z[1]), std::abs(z[2])});
            int exponent = -125;
            if (maximum > 0)
                std::frexp(maximum, &exponent);
            unit = std::ldexp(1., std::max(-149, exponent - 24));
        } else
            unit = 1. / (format == DepthFormat::UNorm16 ? 65535. : 16777215.);
        const float bias = float(double(offset) * unit + double(scale) * slope);
        return clamp > 0 ? std::min(bias, clamp) : clamp < 0 ? std::max(bias, clamp) : bias;
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
