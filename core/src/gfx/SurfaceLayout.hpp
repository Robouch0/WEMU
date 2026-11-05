#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace Core::Gfx {

    struct BaseSurfaceLayout {
            std::uint32_t tileMode, pitch, height, alignment, imageSize;
    };

    // Single-sample thin 2D layouts. Width/height are elements, not BC pixels.
    inline std::optional<BaseSurfaceLayout> baseSurfaceLayout(std::uint32_t width, std::uint32_t height, std::uint32_t depth, std::uint32_t bytes,
                                                              std::uint32_t tileMode, std::uint32_t pixelWidth, std::uint32_t pixelHeight)
    {
        if (!width || !height || !depth || (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8 && bytes != 16))
            return std::nullopt;
        const auto macroPitch = std::max(32u, 128u / bytes);
        if (tileMode == 0)
            tileMode = pixelWidth < macroPitch && pixelHeight < 16 ? 2 : 4;
        std::uint32_t pitchAlign = 1, heightAlign = 1, alignment = 1;
        switch (tileMode) {
            case 16:
                break;
            case 1:
                pitchAlign = std::max(64u, 256u / bytes);
                alignment = 256;
                break;
            case 2:
                pitchAlign = std::max(8u, 32u / bytes);
                heightAlign = 8;
                alignment = 256;
                break;
            case 4:
                pitchAlign = macroPitch;
                heightAlign = 16;
                alignment = macroPitch * 16 * bytes;
                break;
            default:
                return std::nullopt;
        }
        const auto pitch = (std::uint64_t(width) + pitchAlign - 1) & ~std::uint64_t(pitchAlign - 1);
        const auto paddedHeight = (std::uint64_t(height) + heightAlign - 1) & ~std::uint64_t(heightAlign - 1);
        // Reject in stages so even malformed dimensions cannot overflow the product.
        if (pitch > UINT32_MAX || paddedHeight > UINT32_MAX || pitch * paddedHeight > UINT32_MAX / bytes)
            return std::nullopt;
        const auto sliceSize = pitch * paddedHeight * bytes;
        if (sliceSize > UINT32_MAX / depth)
            return std::nullopt;
        return BaseSurfaceLayout{tileMode, std::uint32_t(pitch), std::uint32_t(paddedHeight), alignment, std::uint32_t(sliceSize * depth)};
    }

    inline std::uint32_t pixelIndexInMicroTile(std::uint32_t x, std::uint32_t y, std::uint32_t bpp)
    {
        const std::uint32_t x0 = x & 1, x1 = (x >> 1) & 1, x2 = (x >> 2) & 1;
        const std::uint32_t y0 = y & 1, y1 = (y >> 1) & 1, y2 = (y >> 2) & 1;
        switch (bpp) {
            case 8:
                return x0 | (x1 << 1) | (x2 << 2) | (y1 << 3) | (y0 << 4) | (y2 << 5);
            case 16:
                return x0 | (x1 << 1) | (x2 << 2) | (y0 << 3) | (y1 << 4) | (y2 << 5);
            case 64:
                return x0 | (y0 << 1) | (x1 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5);
            case 128:
                return y0 | (x0 << 1) | (x1 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5);
            default:
                return x0 | (x1 << 1) | (y0 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5);
        }
    }

    // Latte: 2 pipes, 4 banks, 256-byte groups, 8x8 microtiles. Pitch is in
    // elements (one element is a compressed block for BC formats).
    inline std::uint32_t tiledElementOffset(std::uint32_t x, std::uint32_t y, std::uint32_t pitch, std::uint32_t bpp, std::uint32_t tileMode,
                                            std::uint32_t surfSwizzle)
    {
        if (tileMode <= 1 || tileMode == 16)
            return (y * pitch + x) * (bpp / 8);
        const std::uint32_t microTileBytes = (64 * bpp) / 8;
        const std::uint32_t pixelOffset = (pixelIndexInMicroTile(x & 7, y & 7, bpp) * bpp) / 8;
        if (tileMode == 2 || tileMode == 3) {
            return ((y >> 3) * (pitch >> 3) + (x >> 3)) * microTileBytes + pixelOffset;
        }
        constexpr std::uint32_t numPipes = 2, numBanks = 4, numPipeBits = 1, numBankBits = 2, numGroupBits = 8;
        std::uint32_t pipe = ((y >> 3) ^ (x >> 3)) & 1;
        std::uint32_t bank = (((y >> 5) ^ (x >> 3)) & 1) | ((((y >> 4) ^ (x >> 4)) & 1) << 1);
        const std::uint32_t bankPipe =
                ((pipe + numPipes * bank) ^ (((surfSwizzle >> 8) & 1) + numPipes * ((surfSwizzle >> 9) & 3))) % (numPipes * numBanks);
        pipe = bankPipe % numPipes;
        bank = bankPipe / numPipes;
        constexpr std::uint32_t macroTilePitch = 8 * numBanks, macroTileHeight = 8 * numPipes;
        const std::uint32_t macroTileBytes = (bpp * macroTileHeight * macroTilePitch) / 8;
        const std::uint32_t macroTileOffset = ((x / macroTilePitch) + (pitch / macroTilePitch) * (y / macroTileHeight)) * macroTileBytes;
        const std::uint32_t totalOffset = pixelOffset + (macroTileOffset >> (numBankBits + numPipeBits));
        constexpr std::uint32_t groupMask = (1u << numGroupBits) - 1;
        return ((totalOffset & ~groupMask) << (numBankBits + numPipeBits)) | (bank << (numPipeBits + numGroupBits)) | (pipe << numGroupBits) |
               (totalOffset & groupMask);
    }

} // namespace Core::Gfx
