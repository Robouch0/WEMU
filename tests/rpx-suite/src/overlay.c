#include "overlay.h"

#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/utils.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "report.h"
#include "shaders.h"

#define MAX_QUADS 4096
#define ATLAS_SIZE 128 // 16 x 8 glyphs of 8x16
#define SOLID_GLYPH 127

extern const uint8_t wcs_font8x16[128][16];

typedef struct
{
    float position[2], uv[2];
    uint32_t color;
} OverlayVertex;

static Program sProgram;
static GX2Texture sAtlas;
static GX2Sampler sSampler;
static OverlayVertex *sVertices[2];
static uint32_t sQuads, sBuffer;

static const AttribDesc OVERLAY_ATTRIBS[3] = {
    {"aPosition", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1)},
    {"aTexCoord", 8, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1)},
    {"aColor", 16, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A)},
};

bool overlay_init(void)
{
    if (!program_load(&sProgram, SHADER_OVERLAY, OVERLAY_ATTRIBS, 3, sizeof(OverlayVertex)))
        return false;
    for (int b = 0; b < 2; ++b) {
        sVertices[b] = gfx_alloc(MAX_QUADS * 6 * sizeof(OverlayVertex), GX2_VERTEX_BUFFER_ALIGNMENT);
        if (!sVertices[b])
            return report_error("overlay vertex buffer");
    }

    // White glyphs with coverage in alpha; glyph 127 is solid for rectangles.
    memset(&sAtlas, 0, sizeof(sAtlas));
    GX2Surface *surface = &sAtlas.surface;
    surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
    surface->width = surface->height = ATLAS_SIZE;
    surface->depth = surface->mipLevels = 1;
    surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    surface->use = GX2_SURFACE_USE_TEXTURE;
    surface->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(surface);
    surface->image = gfx_alloc(surface->imageSize, surface->alignment);
    if (!surface->image)
        return report_error("overlay atlas");
    uint32_t *texels = surface->image;
    memset(texels, 0, surface->imageSize);
    for (int glyph = 0; glyph < 128; ++glyph)
        for (int row = 0; row < 16; ++row)
            for (int bit = 0; bit < 8; ++bit) {
                bool on = glyph == SOLID_GLYPH || (wcs_font8x16[glyph][row] & (0x80 >> bit));
                uint32_t x = (uint32_t)(glyph % 16) * 8 + (uint32_t)bit, y = (uint32_t)(glyph / 16) * 16 + (uint32_t)row;
                texels[y * surface->pitch + x] = on ? 0xFFFFFFFFu : 0xFFFFFF00u;
            }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, surface->imageSize);
    sAtlas.viewNumMips = sAtlas.viewNumSlices = 1;
    sAtlas.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    GX2InitTextureRegs(&sAtlas);
    GX2InitSampler(&sSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
    return true;
}

void overlay_shutdown(void)
{
    program_free(&sProgram);
    gfx_free(sAtlas.surface.image);
    for (int b = 0; b < 2; ++b)
        gfx_free(sVertices[b]);
}

void overlay_begin(void)
{
    sBuffer ^= 1;
    sQuads = 0;
}

static void quad(float x, float y, float width, float height, int glyph, uint32_t rgba)
{
    if (sQuads >= MAX_QUADS)
        return;
    float u0 = (float)(glyph % 16) * 8.0f / ATLAS_SIZE, v0 = (float)(glyph / 16) * 16.0f / ATLAS_SIZE;
    float u1 = u0 + 8.0f / ATLAS_SIZE, v1 = v0 + 16.0f / ATLAS_SIZE;
    // Virtual 1280x720 pixels (y down) to NDC, so one batch fits the TV and the 16:9 DRC.
    float x0 = x * (2.0f / TV_WIDTH) - 1.0f, x1 = (x + width) * (2.0f / TV_WIDTH) - 1.0f;
    float y0 = 1.0f - y * (2.0f / TV_HEIGHT), y1 = 1.0f - (y + height) * (2.0f / TV_HEIGHT);
    OverlayVertex *v = &sVertices[sBuffer][sQuads++ * 6];
    const float corners[6][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x0, y1, u0, v1},
                                 {x1, y0, u1, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
    for (int i = 0; i < 6; ++i)
        v[i] = (OverlayVertex){{corners[i][0], corners[i][1]}, {corners[i][2], corners[i][3]}, rgba};
}

void overlay_text(float x, float y, float scale, uint32_t rgba, const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    float cursor = x;
    for (const char *c = text; *c; ++c) {
        if (*c == '\n') {
            cursor = x;
            y += 17.0f * scale;
            continue;
        }
        if (*c != ' ')
            quad(cursor, y, 8.0f * scale, 16.0f * scale, *c & 0x7F, rgba);
        cursor += 8.0f * scale;
    }
}

void overlay_rect(float x, float y, float width, float height, uint32_t rgba)
{
    quad(x, y, width, height, SOLID_GLYPH, rgba);
}

void overlay_frame(float x, float y, float width, float height, float thickness, uint32_t rgba)
{
    overlay_rect(x - thickness, y - thickness, width + 2 * thickness, thickness, rgba);
    overlay_rect(x - thickness, y + height, width + 2 * thickness, thickness, rgba);
    overlay_rect(x - thickness, y, thickness, height, rgba);
    overlay_rect(x + width, y, thickness, height, rgba);
}

void overlay_draw(RenderTarget *target)
{
    if (!sQuads)
        return;
    OverlayVertex *v = sVertices[sBuffer];
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, v, sQuads * 6 * sizeof(OverlayVertex));
    target_bind(target);
    program_bind(&sProgram);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
                       GX2_BLEND_COMBINE_MODE_ADD, TRUE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ONE,
                       GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetPixelTexture(&sAtlas, 0);
    GX2SetPixelSampler(&sSampler, 0);
    GX2SetAttribBuffer(0, sQuads * 6 * sizeof(OverlayVertex), sizeof(OverlayVertex), v);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, sQuads * 6, 0, 1);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
}
