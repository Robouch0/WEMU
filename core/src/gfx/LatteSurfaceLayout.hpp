#pragma once

#include <cstdint>
#include <optional>

namespace Core::Gfx {

    // Single-sample Latte layouts. Pitch/height are storage elements (BC blocks
    // for compressed formats); depth includes hardware padding between mip levels.
    struct LatteSurfaceLayout {
            std::uint32_t tileMode{}, pitch{}, height{}, depth{}, alignment{}, imageSize{};
    };
    std::optional<LatteSurfaceLayout> latteSurfaceLayout(unsigned width, unsigned height, unsigned depth, unsigned format, unsigned dimension,
                                                         unsigned tileMode, unsigned level, unsigned use = 1);

    struct LatteHtileLayout {
            std::uint32_t alignment{}, imageSize{};
    };
    std::optional<LatteHtileLayout> latteHtileLayout(unsigned pitch, unsigned height, unsigned slices);

} // namespace Core::Gfx
