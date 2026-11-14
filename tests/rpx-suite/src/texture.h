#pragma once

#include <gx2/texture.h>
#include <stdbool.h>
#include <stdint.h>

// RGBA8 textures built on the CPU in linear staging surfaces and copied into
// GPU-tiled storage with GX2CopySurface, one copy per mip level.
bool texture_checker(GX2Texture *texture, uint32_t size, uint32_t colorA, uint32_t colorB, uint32_t tiles);
// Each mip level filled with MIP_COLORS[level].
extern const uint32_t MIP_COLORS[16];
bool texture_mip_levels(GX2Texture *texture, uint32_t size);
bool texture_solid(GX2Texture *texture, uint32_t rgba);
void texture_free(GX2Texture *texture);
