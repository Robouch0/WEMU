#pragma once

#include <gx2/context.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TV_WIDTH 1280
#define TV_HEIGHT 720
#define DRC_WIDTH 854
#define DRC_HEIGHT 480

typedef struct
{
    const char *name;
    uint32_t offset;
    GX2AttribFormat format;
    uint32_t mask;
    uint32_t buffer;  // attribute buffer slot (default 0)
    bool perInstance; // advance once per instance (divisor 1) instead of per vertex
} AttribDesc;

typedef struct
{
    GX2VertexShader *vs;
    GX2PixelShader *ps;
    GX2FetchShader fs;
    void *fsProgram;
    uint32_t stride;
} Program;

typedef struct
{
    GX2ColorBuffer color;
    GX2DepthBuffer depth;
    GX2Texture colorTexture;
    GX2Texture depthTexture;
    bool hasColor, hasDepth;
} RenderTarget;

bool gfx_init(void);
void gfx_shutdown(void);

// Frame boundary: waits for the previous flip, then resets the uniform ring half.
void gfx_begin_frame(void);
// Copies the TV/DRC targets to their scan buffers and swaps.
void gfx_present(RenderTarget *tv, RenderTarget *drc);
uint32_t gfx_frame_index(void);

void *gfx_alloc(uint32_t size, uint32_t align);
void gfx_free(void *ptr);
// Render targets prefer MEM1 like retail titles; falls back to MEM2 when full.
void *gfx_alloc_mem1(uint32_t size, uint32_t align);
void gfx_reset_mem1(void);

bool program_load(Program *program, const void *gsh, const AttribDesc *attribs, uint32_t count, uint32_t stride);
void program_free(Program *program);
void program_bind(const Program *program);

bool target_init(RenderTarget *target, uint32_t width, uint32_t height, GX2SurfaceFormat color, bool depth,
                 bool sampleDepth);
void target_bind(RenderTarget *target);

// Uniform data is staged in a per-frame ring, byte-swapped to the GPU's
// little-endian order and flushed for the uniform cache.
void *uniform_alloc(uint32_t size);
void uniform_commit(void *block, uint32_t size);
void uniform_vs(uint32_t location, const void *data, uint32_t size);
void uniform_ps(uint32_t location, const void *data, uint32_t size);

void sampler_linear(GX2Sampler *sampler, GX2TexClampMode clamp, bool mips);
void sampler_shadow(GX2Sampler *sampler);

// Synchronous readback of one RGBA8 pixel (copies the target to a linear surface).
uint32_t gfx_read_pixel(RenderTarget *target, uint32_t x, uint32_t y);

// Full-screen quad for post passes; pair with FULLSCREEN_ATTRIBS (stride 16).
extern const AttribDesc FULLSCREEN_ATTRIBS[2];
void fullscreen_draw(void);
