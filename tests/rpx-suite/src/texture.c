#include "texture.h"

#include <coreinit/cache.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/utils.h>
#include <string.h>

#include "gfx.h"
#include "report.h"

static void surface_init(GX2Surface *surface, uint32_t width, uint32_t height, uint32_t mips, GX2TileMode tile)
{
    memset(surface, 0, sizeof(*surface));
    surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
    surface->width = width;
    surface->height = height;
    surface->depth = 1;
    surface->mipLevels = mips;
    surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    surface->aa = GX2_AA_MODE1X;
    surface->use = GX2_SURFACE_USE_TEXTURE;
    surface->tileMode = tile;
    GX2CalcSurfaceSizeAndAlignment(surface);
}

static uint32_t mip_count(uint32_t size)
{
    uint32_t count = 1;
    while (size > 1) {
        size >>= 1;
        ++count;
    }
    return count;
}

static uint8_t channel(uint32_t rgba, int shift)
{
    return (uint8_t)(rgba >> shift);
}

// Builds level 0 in `pixels` (size x size RGBA8), then box-filters down the chain,
// copying each level through its own linear staging surface.
static bool upload(GX2Texture *texture, uint8_t *pixels, uint32_t size, bool levelColors)
{
    uint32_t mips = mip_count(size);
    memset(texture, 0, sizeof(*texture));
    surface_init(&texture->surface, size, size, mips, GX2_TILE_MODE_DEFAULT);
    texture->surface.image = gfx_alloc(texture->surface.imageSize, texture->surface.alignment);
    if (texture->surface.mipmapSize)
        texture->surface.mipmaps = gfx_alloc(texture->surface.mipmapSize, texture->surface.alignment);
    if (!texture->surface.image || (texture->surface.mipmapSize && !texture->surface.mipmaps))
        return report_error("texture allocation %u", (unsigned)size);
    texture->viewNumMips = mips;
    texture->viewNumSlices = 1;
    texture->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    GX2InitTextureRegs(texture);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, texture->surface.image, texture->surface.imageSize);
    if (texture->surface.mipmaps)
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, texture->surface.mipmaps, texture->surface.mipmapSize);

    void *staging[16] = {0};
    uint32_t width = size;
    for (uint32_t level = 0; level < mips; ++level, width = width > 1 ? width >> 1 : 1) {
        if (levelColors) {
            for (uint32_t i = 0; i < width * width; ++i)
                for (int c = 0; c < 4; ++c)
                    pixels[i * 4 + c] = channel(MIP_COLORS[level], 24 - 8 * c);
        } else if (level) {
            // pixels holds the previous level (2*width square); filter in place.
            uint32_t previous = width * 2;
            for (uint32_t y = 0; y < width; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    for (int c = 0; c < 4; ++c) {
                        uint32_t sum = pixels[((2 * y) * previous + 2 * x) * 4 + c] +
                                       pixels[((2 * y) * previous + 2 * x + 1) * 4 + c] +
                                       pixels[((2 * y + 1) * previous + 2 * x) * 4 + c] +
                                       pixels[((2 * y + 1) * previous + 2 * x + 1) * 4 + c];
                        pixels[(y * width + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
                    }
        }
        GX2Surface linear;
        surface_init(&linear, width, width, 1, GX2_TILE_MODE_LINEAR_ALIGNED);
        linear.image = staging[level] = gfx_alloc(linear.imageSize, linear.alignment);
        if (!linear.image)
            return report_error("texture staging allocation");
        for (uint32_t y = 0; y < width; ++y)
            memcpy((uint8_t *)linear.image + y * linear.pitch * 4, pixels + y * width * 4, width * 4);
        DCFlushRange(linear.image, linear.imageSize);
        GX2CopySurface(&linear, 0, 0, &texture->surface, level, 0);
    }
    GX2DrawDone();
    for (uint32_t level = 0; level < mips; ++level)
        gfx_free(staging[level]);
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, texture->surface.image, texture->surface.imageSize);
    return true;
}

bool texture_checker(GX2Texture *texture, uint32_t size, uint32_t colorA, uint32_t colorB, uint32_t tiles)
{
    uint8_t *pixels = gfx_alloc(size * size * 4, 64);
    if (!pixels)
        return report_error("checker allocation");
    uint32_t cell = size / tiles;
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            uint32_t color = (((x / cell) ^ (y / cell)) & 1) ? colorA : colorB;
            // A soft diagonal gradient so mip levels differ from each other.
            uint32_t shade = 200 + ((x + y) * 55) / (2 * size);
            uint8_t *p = pixels + (y * size + x) * 4;
            p[0] = (uint8_t)(channel(color, 24) * shade / 255);
            p[1] = (uint8_t)(channel(color, 16) * shade / 255);
            p[2] = (uint8_t)(channel(color, 8) * shade / 255);
            p[3] = channel(color, 0);
        }
    bool ok = upload(texture, pixels, size, false);
    gfx_free(pixels);
    return ok;
}

const uint32_t MIP_COLORS[16] = {0xFF0000FF, 0x00FF00FF, 0x0000FFFF, 0xFFFF00FF, 0xFF00FFFF, 0x00FFFFFF, 0x808080FF, 0xFF8000FF,
                                 0x8000FFFF, 0x00FF80FF, 0xFFFFFFFF, 0x404040FF, 0x80FF00FF, 0x0080FFFF, 0xFF0080FF, 0x000000FF};

bool texture_mip_levels(GX2Texture *texture, uint32_t size)
{
    uint8_t *pixels = gfx_alloc(size * size * 4, 64);
    if (!pixels)
        return report_error("mip texture allocation");
    bool ok = upload(texture, pixels, size, true);
    gfx_free(pixels);
    return ok;
}

bool texture_solid(GX2Texture *texture, uint32_t rgba)
{
    uint8_t pixels[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
        pixels[i * 4] = channel(rgba, 24);
        pixels[i * 4 + 1] = channel(rgba, 16);
        pixels[i * 4 + 2] = channel(rgba, 8);
        pixels[i * 4 + 3] = channel(rgba, 0);
    }
    return upload(texture, pixels, 4, false);
}

void texture_free(GX2Texture *texture)
{
    gfx_free(texture->surface.image);
    gfx_free(texture->surface.mipmaps);
    memset(texture, 0, sizeof(*texture));
}
