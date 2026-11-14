#include "formats.h"

#include <coreinit/cache.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/utils.h>
#include <string.h>

#include "gfx.h"
#include "report.h"

#define RGBA GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A)
#define RRR1 GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_1)
#define RG01 GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_0, GX2_SQ_SEL_1)

const TextureFormatInfo TEXTURE_FORMATS[] = {
    {"R8", GX2_SURFACE_FORMAT_UNORM_R8, {8, 0, 0, 0}, 0, 1, RRR1},
    {"RG8", GX2_SURFACE_FORMAT_UNORM_R8_G8, {8, 8, 0, 0}, 0, 2, RG01},
    {"RGB565", GX2_SURFACE_FORMAT_UNORM_R5_G6_B5, {5, 6, 5, 0}, 0, 2, RGBA},
    {"RGB5A1", GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1, {5, 5, 5, 1}, 0, 2, RGBA},
    {"RGBA4", GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4, {4, 4, 4, 4}, 0, 2, RGBA},
    {"RGBA8", GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, {8, 8, 8, 8}, 0, 4, RGBA},
    {"RGB10A2", GX2_SURFACE_FORMAT_UNORM_R10_G10_B10_A2, {10, 10, 10, 2}, 0, 4, RGBA},
    {"BC1", GX2_SURFACE_FORMAT_UNORM_BC1, {5, 6, 5, 0}, 8, 0, RGBA},
    {"BC3", GX2_SURFACE_FORMAT_UNORM_BC3, {5, 6, 5, 8}, 16, 0, RGBA},
    {"BC4", GX2_SURFACE_FORMAT_UNORM_BC4, {8, 0, 0, 0}, 8, 0, RRR1},
    {"BC5", GX2_SURFACE_FORMAT_UNORM_BC5, {8, 8, 0, 0}, 16, 0, RG01},
};
const uint32_t TEXTURE_FORMAT_COUNT = sizeof(TEXTURE_FORMATS) / sizeof(TEXTURE_FORMATS[0]);

static uint32_t quantize(uint8_t value, int bits)
{
    uint32_t max = (1u << bits) - 1;
    return (value * max + 127) / 255;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static void encode_texel(const TextureFormatInfo *f, const uint8_t *c, uint8_t *out)
{
    switch (f->format) {
    case GX2_SURFACE_FORMAT_UNORM_R8:
        out[0] = c[0];
        break;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8:
        out[0] = c[0];
        out[1] = c[1];
        break;
    case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5:
        put16(out, quantize(c[0], 5) | quantize(c[1], 6) << 5 | quantize(c[2], 5) << 11);
        break;
    case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1:
        put16(out, quantize(c[0], 5) | quantize(c[1], 5) << 5 | quantize(c[2], 5) << 10 | quantize(c[3], 1) << 15);
        break;
    case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4:
        put16(out, quantize(c[0], 4) | quantize(c[1], 4) << 4 | quantize(c[2], 4) << 8 | quantize(c[3], 4) << 12);
        break;
    case GX2_SURFACE_FORMAT_UNORM_R10_G10_B10_A2:
        put32(out, quantize(c[0], 10) | quantize(c[1], 10) << 10 | quantize(c[2], 10) << 20 | quantize(c[3], 2) << 30);
        break;
    default:
        memcpy(out, c, 4);
        break;
    }
}

// Standard BC 5:6:5 endpoint (R in the high bits, unlike FMT_5_6_5 surfaces).
static uint32_t bc565(const uint8_t *c)
{
    return quantize(c[0], 5) << 11 | quantize(c[1], 6) << 5 | quantize(c[2], 5);
}

static void bc565_expand(uint32_t v, int out[3])
{
    out[0] = (int)(((v >> 11) & 31) * 255 + 15) / 31;
    out[1] = (int)(((v >> 5) & 63) * 255 + 31) / 63;
    out[2] = (int)((v & 31) * 255 + 15) / 31;
}

// 4x4 colour block, 4-colour mode: endpoints from the brightest/darkest texel.
static void encode_bc1_block(const uint8_t texels[16][4], uint8_t *out)
{
    int hi = 0, lo = 0;
    for (int i = 1; i < 16; ++i) {
        int lum = texels[i][0] * 3 + texels[i][1] * 6 + texels[i][2];
        if (lum > texels[hi][0] * 3 + texels[hi][1] * 6 + texels[hi][2])
            hi = i;
        if (lum < texels[lo][0] * 3 + texels[lo][1] * 6 + texels[lo][2])
            lo = i;
    }
    uint32_t c0 = bc565(texels[hi]), c1 = bc565(texels[lo]), indices = 0;
    if (c0 < c1) {
        uint32_t t = c0;
        c0 = c1;
        c1 = t;
    }
    put16(out, c0);
    put16(out + 2, c1);
    if (c0 != c1) {
        int e0[3], e1[3];
        bc565_expand(c0, e0);
        bc565_expand(c1, e1);
        for (int i = 0; i < 16; ++i) {
            int best = 0, bestError = 1 << 30;
            for (int k = 0; k < 4; ++k) {
                static const int weights[4][2] = {{3, 0}, {0, 3}, {2, 1}, {1, 2}};
                int error = 0;
                for (int ch = 0; ch < 3; ++ch) {
                    int palette = (e0[ch] * weights[k][0] + e1[ch] * weights[k][1]) / 3;
                    error += (palette - texels[i][ch]) * (palette - texels[i][ch]);
                }
                if (error < bestError) {
                    bestError = error;
                    best = k;
                }
            }
            indices |= (uint32_t)best << (2 * i);
        }
    }
    put32(out + 4, indices);
}

// Single-channel 8-value block (BC3 alpha, BC4, BC5 channels).
static void encode_channel_block(const uint8_t texels[16][4], int channel, uint8_t *out)
{
    uint8_t hi = 0, lo = 255;
    for (int i = 0; i < 16; ++i) {
        uint8_t v = texels[i][channel];
        hi = v > hi ? v : hi;
        lo = v < lo ? v : lo;
    }
    out[0] = hi;
    out[1] = lo;
    uint64_t indices = 0;
    if (hi != lo)
        for (int i = 0; i < 16; ++i) {
            // Palette: 0 = hi, 1 = lo, 2..7 = hi->lo in sevenths.
            int t = ((hi - texels[i][channel]) * 7 + (hi - lo) / 2) / (hi - lo);
            int index = t == 0 ? 0 : (t == 7 ? 1 : t + 1);
            indices |= (uint64_t)index << (3 * i);
        }
    for (int b = 0; b < 6; ++b)
        out[2 + b] = (uint8_t)(indices >> (8 * b));
}

static void encode_block(const TextureFormatInfo *f, const uint8_t texels[16][4], uint8_t *out)
{
    switch (f->format) {
    case GX2_SURFACE_FORMAT_UNORM_BC1:
        encode_bc1_block(texels, out);
        break;
    case GX2_SURFACE_FORMAT_UNORM_BC3:
        encode_channel_block(texels, 3, out);
        encode_bc1_block(texels, out + 8);
        break;
    case GX2_SURFACE_FORMAT_UNORM_BC4:
        encode_channel_block(texels, 0, out);
        break;
    default: // BC5
        encode_channel_block(texels, 0, out);
        encode_channel_block(texels, 1, out + 8);
        break;
    }
}

static void surface_init(GX2Surface *surface, const TextureFormatInfo *f, uint32_t width, uint32_t height,
                         GX2TileMode tile)
{
    memset(surface, 0, sizeof(*surface));
    surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
    surface->width = width;
    surface->height = height;
    surface->depth = 1;
    surface->mipLevels = 1;
    surface->format = f->format;
    surface->aa = GX2_AA_MODE1X;
    surface->use = GX2_SURFACE_USE_TEXTURE;
    surface->tileMode = tile;
    GX2CalcSurfaceSizeAndAlignment(surface);
}

bool texture_from_rgba(GX2Texture *texture, const TextureFormatInfo *f, uint32_t width, uint32_t height,
                       const uint8_t *rgba)
{
    memset(texture, 0, sizeof(*texture));
    GX2Surface linear;
    surface_init(&linear, f, width, height, GX2_TILE_MODE_LINEAR_ALIGNED);
    uint8_t *data = gfx_alloc(linear.imageSize, linear.alignment);
    if (!data)
        return report_error("%s texture %ux%u", f->name, (unsigned)width, (unsigned)height);
    memset(data, 0, linear.imageSize);
    linear.image = data;
    if (f->blockBytes) {
        for (uint32_t by = 0; by < height / 4; ++by)
            for (uint32_t bx = 0; bx < width / 4; ++bx) {
                uint8_t texels[16][4];
                for (int i = 0; i < 16; ++i)
                    memcpy(texels[i], rgba + ((by * 4 + (uint32_t)i / 4) * width + bx * 4 + (uint32_t)i % 4) * 4, 4);
                encode_block(f, texels, data + (by * linear.pitch + bx) * f->blockBytes);
            }
    } else {
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                encode_texel(f, rgba + (y * width + x) * 4, data + (y * linear.pitch + x) * f->texelBytes);
    }
    DCFlushRange(data, linear.imageSize);

    if (f->blockBytes) {
        // Block-compressed data stays linear (GX2CopySurface does not convert BC tiling).
        texture->surface = linear;
    } else {
        surface_init(&texture->surface, f, width, height, GX2_TILE_MODE_DEFAULT);
        texture->surface.image = gfx_alloc(texture->surface.imageSize, texture->surface.alignment);
        if (!texture->surface.image)
            return report_error("%s tiled texture", f->name);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, texture->surface.image, texture->surface.imageSize);
        GX2CopySurface(&linear, 0, 0, &texture->surface, 0, 0);
        GX2DrawDone();
        gfx_free(data);
    }
    texture->viewNumMips = 1;
    texture->viewNumSlices = 1;
    texture->compMap = f->compMap;
    GX2InitTextureRegs(texture);
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, texture->surface.image, texture->surface.imageSize);
    return true;
}

void format_expected(const TextureFormatInfo *f, const uint8_t rgba[4], float out[3])
{
    float decoded[4] = {0, 0, 0, 1};
    if (f->format == GX2_SURFACE_FORMAT_UNORM_BC1 || f->format == GX2_SURFACE_FORMAT_UNORM_BC3) {
        int e[3];
        bc565_expand(bc565(rgba), e);
        for (int c = 0; c < 3; ++c)
            decoded[c] = (float)e[c] / 255.0f;
    } else {
        for (int c = 0; c < 4; ++c)
            if (f->bits[c])
                decoded[c] = (float)quantize(rgba[c], f->bits[c]) / (float)((1u << f->bits[c]) - 1);
    }
    for (int c = 0; c < 3; ++c) {
        uint32_t select = (f->compMap >> (24 - 8 * c)) & 7;
        out[c] = select < 4 ? decoded[select] : (select == GX2_SQ_SEL_1 ? 1.0f : 0.0f);
    }
}
