#pragma once

#include "gfx/TextureSampler.hpp"

namespace Core::Gfx {
    // Compose the existing address/filter rules on XY and Z. Fetch returns an
    // unfiltered, component-mapped texel; W is normalized, unlike array layers.
    template<class Fetch>
    std::array<float, 4> sampleVolume(const TextureSampler &sampler, unsigned width, unsigned height, unsigned depth, float u, float v, float w,
                                      const Fetch &fetch)
    {
        auto axis = sampler;
        axis.regs[0] = (axis.regs[0] & ~63u) | ((sampler.regs[0] >> 6) & 7) | (2u << 3);
        return axis.sample(depth, 1, w, .5f, [&](unsigned z, unsigned) {
            return sampler.sample(width, height, u, v, [&](unsigned x, unsigned y) { return fetch(x, y, z); });
        });
    }
} // namespace Core::Gfx
