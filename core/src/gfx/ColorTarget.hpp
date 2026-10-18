#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Core::Gfx {
    enum class ColorFormat { RGBA8, RGBA32Float, R11G11B10Float, RGBA16Float };
    constexpr unsigned colorElementBytes(ColorFormat format)
    {
        return format == ColorFormat::RGBA32Float ? 16u : format == ColorFormat::RGBA16Float ? 8u : 4u;
    }
    constexpr ColorFormat colorFormat(unsigned gx2Format)
    {
        return gx2Format == 0x823   ? ColorFormat::RGBA32Float
               : gx2Format == 0x816 ? ColorFormat::R11G11B10Float
               : gx2Format == 0x820 ? ColorFormat::RGBA16Float
                                    : ColorFormat::RGBA8;
    }
    // IEEE binary16, as stored by 0x820 (R16G16B16A16_FLOAT) surfaces.
    inline float decodeHalf(std::uint16_t bits)
    {
        const std::uint32_t sign = (bits & 0x8000u) << 16, exponent = (bits >> 10) & 0x1F, mantissa = bits & 0x3FF;
        if (!exponent)
            return std::bit_cast<float>(sign) + (sign ? -1.f : 1.f) * std::ldexp(float(mantissa), -24);
        if (exponent == 31)
            return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
        return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
    }
    // Round to nearest even; overflow becomes infinity and NaN stays NaN.
    inline std::uint16_t encodeHalf(float value)
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        const std::uint32_t sign = (bits >> 16) & 0x8000u;
        const std::uint32_t magnitude = bits & 0x7FFFFFFFu;
        if (magnitude > 0x7F800000u)
            return std::uint16_t(sign | 0x7E00u);
        if (magnitude >= 0x477FF000u) // rounds to at least 65520: infinity
            return std::uint16_t(sign | 0x7C00u);
        if (magnitude < 0x38800000u) { // subnormal or zero
            const auto scaled = double(std::bit_cast<float>(magnitude)) * 16777216.0; // 2^24
            const auto floor = std::uint32_t(scaled);
            const auto remainder = scaled - floor;
            return std::uint16_t(sign | (floor + std::uint32_t(remainder > .5 || (remainder == .5 && (floor & 1)))));
        }
        const auto rounded = magnitude - 0x38000000u + 0xFFFu + ((magnitude >> 13) & 1u);
        return std::uint16_t(sign | (rounded >> 13));
    }
    // Unsigned five-bit exponent, bias 15; R/G have six fraction bits, B has five.
    inline float decodeUnsignedFloat(unsigned bits, unsigned fractionBits)
    {
        const auto exponent = bits >> fractionBits;
        const auto fraction = bits & ((1u << fractionBits) - 1);
        if (exponent == 31)
            return std::bit_cast<float>(0x7F800000u | (fraction << (23 - fractionBits)));
        if (!exponent)
            return std::ldexp(float(fraction), -14 - int(fractionBits));
        return std::ldexp(1.f + float(fraction) / float(1u << fractionBits), int(exponent) - 15);
    }
    inline unsigned encodeUnsignedFloat(float value, unsigned fractionBits)
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        if (std::isnan(value))
            return (31u << fractionBits) | (1u << (fractionBits - 1));
        if (value <= 0.f)
            return 0;
        const int exponent = int((bits >> 23) & 255) - 127 + 15;
        if (exponent >= 31)
            return 31u << fractionBits;
        if (exponent <= 0) {
            const double scaled = std::ldexp(double(value), 14 + int(fractionBits));
            const auto floor = unsigned(scaled);
            const auto remainder = scaled - floor;
            return floor + unsigned(remainder > .5 || (remainder == .5 && (floor & 1)));
        }
        const unsigned shift = 23 - fractionBits;
        const auto fraction = bits & 0x7FFFFFu;
        const auto rounded = (fraction + (1u << (shift - 1)) - 1 + ((fraction >> shift) & 1)) >> shift;
        // A fraction carry also carries into the exponent (including infinity).
        return (unsigned(exponent) << fractionBits) + rounded;
    }
    inline std::array<float, 4> readFloatColor(const std::uint8_t *bytes, ColorFormat format = ColorFormat::RGBA32Float)
    {
        if (format == ColorFormat::R11G11B10Float) {
            const auto bits =
                    std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
            return {decodeUnsignedFloat(bits & 0x7FF, 6), decodeUnsignedFloat((bits >> 11) & 0x7FF, 6), decodeUnsignedFloat(bits >> 22, 5), 1.f};
        }
        if (format == ColorFormat::RGBA16Float) {
            std::array<float, 4> value;
            for (unsigned c = 0; c < 4; ++c)
                value[c] = decodeHalf(std::uint16_t(bytes[c * 2] | (bytes[c * 2 + 1] << 8)));
            return value;
        }
        std::array<float, 4> value;
        std::memcpy(value.data(), bytes, sizeof(value));
        return value;
    }
    inline void writeFloatColor(std::uint8_t *bytes, const std::array<float, 4> &value, unsigned mask, ColorFormat format = ColorFormat::RGBA32Float)
    {
        if (format == ColorFormat::R11G11B10Float) {
            auto bits = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
            constexpr unsigned shifts[]{0, 11, 22}, masks[]{0x7FF, 0x7FF, 0x3FF};
            for (unsigned c = 0; c < 3; ++c)
                if (mask & (1u << c))
                    bits = (bits & ~(masks[c] << shifts[c])) | (encodeUnsignedFloat(value[c], c == 2 ? 5 : 6) << shifts[c]);
            for (unsigned b = 0; b < 4; ++b)
                bytes[b] = bits >> (b * 8);
            return;
        }
        if (format == ColorFormat::RGBA16Float) {
            for (unsigned c = 0; c < 4; ++c)
                if (mask & (1u << c)) {
                    const auto half = encodeHalf(value[c]);
                    bytes[c * 2] = std::uint8_t(half);
                    bytes[c * 2 + 1] = std::uint8_t(half >> 8);
                }
            return;
        }
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
