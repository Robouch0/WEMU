#pragma once

#include <gx2/texture.h>
#include <stdbool.h>
#include <stdint.h>

// Texture formats exercised by the `textures` scene. Encoders follow Latte's
// layout: packed words are GPU little-endian with R in the low bits; BC blocks
// use the standard DXT/RGTC layout.
typedef struct
{
    const char *name;
    GX2SurfaceFormat format;
    uint8_t bits[4]; // per channel, 0 = absent
    uint8_t blockBytes; // 0 = uncompressed
    uint8_t texelBytes;
    uint32_t compMap;
} TextureFormatInfo;

extern const TextureFormatInfo TEXTURE_FORMATS[];
extern const uint32_t TEXTURE_FORMAT_COUNT;

// Encodes `rgba` (width x height RGBA8) into `format` and uploads it: compressed
// formats as linear-aligned surfaces, others copied into GPU tiling.
bool texture_from_rgba(GX2Texture *texture, const TextureFormatInfo *format, uint32_t width, uint32_t height,
                       const uint8_t *rgba);
// Colour the shader sees for a solid `rgba` texel after encode, decode and the comp map.
void format_expected(const TextureFormatInfo *format, const uint8_t rgba[4], float out[3]);
