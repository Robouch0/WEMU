#include "gfx.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <gfd.h>
#include <gx2/clear.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <gx2/utils.h>
#include <string.h>

#include "report.h"

#define COMMAND_POOL_SIZE (4u << 20)
#define UNIFORM_RING_SIZE (2u << 20) // per frame half

static void *sCommandPool;
static GX2ContextState *sContext;
static void *sTvScan, *sDrcScan;
static MEMHeapHandle sMem1, sForeground;
static uint8_t *sUniformRing;
static uint32_t sUniformUsed, sFrame;
static void *sStaging;
static uint32_t sStagingSize;

static float *sQuad;

const AttribDesc FULLSCREEN_ATTRIBS[2] = {
    {"aPosition", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1)},
    {"aTexCoord", 8, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1)},
};

void *gfx_alloc(uint32_t size, uint32_t align)
{
    return MEMAllocFromDefaultHeapEx(size, (int32_t)align);
}

void gfx_free(void *ptr)
{
    if (ptr)
        MEMFreeToDefaultHeap(ptr);
}

void *gfx_alloc_mem1(uint32_t size, uint32_t align)
{
    void *ptr = sMem1 ? MEMAllocFromFrmHeapEx(sMem1, size, (int)align) : NULL;
    return ptr ? ptr : gfx_alloc(size, align);
}

void gfx_reset_mem1(void)
{
    if (sMem1)
        MEMFreeToFrmHeap(sMem1, MEM_FRM_HEAP_FREE_ALL);
}

bool gfx_init(void)
{
    sCommandPool = gfx_alloc(COMMAND_POOL_SIZE, GX2_COMMAND_BUFFER_ALIGNMENT);
    if (!sCommandPool)
        return false;
    uint32_t attributes[] = {GX2_INIT_CMD_BUF_BASE, (uintptr_t)sCommandPool, GX2_INIT_CMD_BUF_POOL_SIZE,
                             COMMAND_POOL_SIZE, GX2_INIT_ARGC, 0, GX2_INIT_ARGV, 0, GX2_INIT_END};
    GX2Init(attributes);

    sMem1 = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    sForeground = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);

    uint32_t size, scale;
    GX2CalcTVSize(GX2_TV_RENDER_MODE_WIDE_720P, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE,
                  &size, &scale);
    sTvScan = sForeground ? MEMAllocFromFrmHeapEx(sForeground, size, GX2_SCAN_BUFFER_ALIGNMENT) : NULL;
    if (!sTvScan)
        sTvScan = gfx_alloc(size, GX2_SCAN_BUFFER_ALIGNMENT);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, sTvScan, size);
    GX2SetTVBuffer(sTvScan, size, GX2_TV_RENDER_MODE_WIDE_720P, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                   GX2_BUFFERING_MODE_DOUBLE);
    GX2SetTVScale(TV_WIDTH, TV_HEIGHT);

    GX2CalcDRCSize(GX2_DRC_RENDER_MODE_SINGLE, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE,
                   &size, &scale);
    sDrcScan = sForeground ? MEMAllocFromFrmHeapEx(sForeground, size, GX2_SCAN_BUFFER_ALIGNMENT) : NULL;
    if (!sDrcScan)
        sDrcScan = gfx_alloc(size, GX2_SCAN_BUFFER_ALIGNMENT);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, sDrcScan, size);
    GX2SetDRCBuffer(sDrcScan, size, GX2_DRC_RENDER_MODE_SINGLE, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                    GX2_BUFFERING_MODE_DOUBLE);
    GX2SetDRCScale(DRC_WIDTH, DRC_HEIGHT);

    sContext = gfx_alloc(sizeof(GX2ContextState), GX2_CONTEXT_STATE_ALIGNMENT);
    if (!sContext)
        return false;
    GX2SetupContextStateEx(sContext, TRUE);
    GX2SetContextState(sContext);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
    GX2SetSwapInterval(1);

    sUniformRing = gfx_alloc(UNIFORM_RING_SIZE * 2, GX2_UNIFORM_BLOCK_ALIGNMENT);
    if (!sUniformRing)
        return false;

    // Two triangles, (x, y, u, v): v grows downwards like render-target rows.
    static const float quad[] = {-1, 1, 0, 0, 1, 1, 1, 0, -1, -1, 0, 1, 1, 1, 1, 0, 1, -1, 1, 1, -1, -1, 0, 1};
    sQuad = gfx_alloc(sizeof(quad), GX2_VERTEX_BUFFER_ALIGNMENT);
    memcpy(sQuad, quad, sizeof(quad));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, sQuad, sizeof(quad));

    GX2SetTVEnable(TRUE);
    GX2SetDRCEnable(TRUE);
    return true;
}

void gfx_shutdown(void)
{
    GX2DrawDone();
    GX2Shutdown();
    gfx_free(sStaging);
    gfx_free(sQuad);
    gfx_free(sUniformRing);
    gfx_free(sContext);
    gfx_free(sCommandPool);
}

void gfx_begin_frame(void)
{
    if (sFrame)
        GX2WaitForFlip();
    sUniformUsed = 0;
    GX2SetContextState(sContext);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
}

void gfx_present(RenderTarget *tv, RenderTarget *drc)
{
    GX2CopyColorBufferToScanBuffer(&tv->color, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(drc ? &drc->color : &tv->color, GX2_SCAN_TARGET_DRC);
    GX2SwapScanBuffers();
    GX2Flush();
    ++sFrame;
}

uint32_t gfx_frame_index(void)
{
    return sFrame;
}

static int32_t attrib_location(const GX2VertexShader *shader, const char *name)
{
    for (uint32_t i = 0; i < shader->attribVarCount; ++i)
        if (strcmp(shader->attribVars[i].name, name) == 0)
            return (int32_t)shader->attribVars[i].location;
    return -1;
}

bool program_load(Program *program, const void *gsh, const AttribDesc *attribs, uint32_t count, uint32_t stride)
{
    memset(program, 0, sizeof(*program));
    uint32_t header = GFDGetVertexShaderHeaderSize(0, gsh), code = GFDGetVertexShaderProgramSize(0, gsh);
    program->vs = gfx_alloc(header, 64);
    void *vsCode = gfx_alloc(code, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!program->vs || !vsCode || !GFDGetVertexShader(program->vs, vsCode, 0, gsh))
        return report_error("GFD vertex shader: %s", GFDGetLastErrorString());
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, vsCode, code);

    header = GFDGetPixelShaderHeaderSize(0, gsh);
    code = GFDGetPixelShaderProgramSize(0, gsh);
    program->ps = gfx_alloc(header, 64);
    void *psCode = gfx_alloc(code, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!program->ps || !psCode || !GFDGetPixelShader(program->ps, psCode, 0, gsh))
        return report_error("GFD pixel shader: %s", GFDGetLastErrorString());
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, psCode, code);

    GX2AttribStream streams[8];
    if (count > 8)
        return report_error("too many attributes");
    for (uint32_t i = 0; i < count; ++i) {
        int32_t location = attrib_location(program->vs, attribs[i].name);
        if (location < 0)
            return report_error("attribute %s not in shader", attribs[i].name);
        streams[i] = (GX2AttribStream){.location = (uint32_t)location,
                                       .buffer = attribs[i].buffer,
                                       .offset = attribs[i].offset,
                                       .format = attribs[i].format,
                                       .type = attribs[i].perInstance ? GX2_ATTRIB_INDEX_PER_INSTANCE
                                                                      : GX2_ATTRIB_INDEX_PER_VERTEX,
                                       .aluDivisor = attribs[i].perInstance ? 1 : 0,
                                       .mask = attribs[i].mask,
                                       .endianSwap = GX2_ENDIAN_SWAP_DEFAULT};
    }
    uint32_t fsSize = GX2CalcFetchShaderSizeEx(count, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    program->fsProgram = gfx_alloc(fsSize, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!program->fsProgram)
        return report_error("fetch shader allocation");
    GX2InitFetchShaderEx(&program->fs, program->fsProgram, count, streams, GX2_FETCH_SHADER_TESSELLATION_NONE,
                         GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, program->fsProgram, fsSize);
    program->stride = stride;
    return true;
}

void program_free(Program *program)
{
    if (program->vs)
        gfx_free(program->vs->program);
    if (program->ps)
        gfx_free(program->ps->program);
    gfx_free(program->vs);
    gfx_free(program->ps);
    gfx_free(program->fsProgram);
    memset(program, 0, sizeof(*program));
}

void program_bind(const Program *program)
{
    GX2SetFetchShader(&program->fs);
    GX2SetVertexShader(program->vs);
    GX2SetPixelShader(program->ps);
}

static void surface_setup(GX2Surface *surface, uint32_t width, uint32_t height, GX2SurfaceFormat format,
                          GX2SurfaceUse use)
{
    memset(surface, 0, sizeof(*surface));
    surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
    surface->width = width;
    surface->height = height;
    surface->depth = 1;
    surface->mipLevels = 1;
    surface->format = format;
    surface->aa = GX2_AA_MODE1X;
    surface->use = use;
    surface->tileMode = GX2_TILE_MODE_DEFAULT;
    GX2CalcSurfaceSizeAndAlignment(surface);
}

static void texture_view(GX2Texture *texture, const GX2Surface *surface, uint32_t compMap)
{
    memset(texture, 0, sizeof(*texture));
    texture->surface = *surface;
    texture->viewNumMips = 1;
    texture->viewNumSlices = 1;
    texture->compMap = compMap;
    GX2InitTextureRegs(texture);
}

bool target_init(RenderTarget *target, uint32_t width, uint32_t height, GX2SurfaceFormat color, bool depth,
                 bool sampleDepth)
{
    memset(target, 0, sizeof(*target));
    if (color) {
        surface_setup(&target->color.surface, width, height, color, GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV);
        target->color.viewNumSlices = 1;
        target->color.surface.image = gfx_alloc_mem1(target->color.surface.imageSize, target->color.surface.alignment);
        if (!target->color.surface.image)
            return report_error("colour target %ux%u allocation", width, height);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, target->color.surface.image, target->color.surface.imageSize);
        GX2InitColorBufferRegs(&target->color);
        texture_view(&target->colorTexture, &target->color.surface,
                     GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A));
        target->hasColor = true;
    }
    if (depth) {
        GX2SurfaceUse use = GX2_SURFACE_USE_DEPTH_BUFFER | (sampleDepth ? GX2_SURFACE_USE_TEXTURE : 0);
        surface_setup(&target->depth.surface, width, height, GX2_SURFACE_FORMAT_FLOAT_R32, use);
        target->depth.viewNumSlices = 1;
        target->depth.depthClear = 1.0f;
        target->depth.surface.image = gfx_alloc_mem1(target->depth.surface.imageSize, target->depth.surface.alignment);
        if (!target->depth.surface.image)
            return report_error("depth target %ux%u allocation", width, height);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, target->depth.surface.image, target->depth.surface.imageSize);
        GX2InitDepthBufferRegs(&target->depth);
        if (sampleDepth)
            texture_view(&target->depthTexture, &target->depth.surface,
                         GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1));
        target->hasDepth = true;
    }
    return true;
}

void target_bind(RenderTarget *target)
{
    GX2Surface *surface = target->hasColor ? &target->color.surface : &target->depth.surface;
    if (target->hasColor)
        GX2SetColorBuffer(&target->color, GX2_RENDER_TARGET_0);
    if (target->hasDepth)
        GX2SetDepthBuffer(&target->depth);
    GX2SetViewport(0, 0, (float)surface->width, (float)surface->height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, surface->width, surface->height);
}

void *uniform_alloc(uint32_t size)
{
    size = (size + GX2_UNIFORM_BLOCK_ALIGNMENT - 1) & ~(GX2_UNIFORM_BLOCK_ALIGNMENT - 1);
    if (sUniformUsed + size > UNIFORM_RING_SIZE) {
        report_error("uniform ring overflow");
        sUniformUsed = 0;
    }
    uint8_t *block = sUniformRing + (sFrame & 1) * UNIFORM_RING_SIZE + sUniformUsed;
    sUniformUsed += size;
    return block;
}

void uniform_commit(void *block, uint32_t size)
{
    uint32_t *words = block;
    for (uint32_t i = 0; i < size / 4; ++i)
        words[i] = __builtin_bswap32(words[i]);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK, block, size);
}

void uniform_vs(uint32_t location, const void *data, uint32_t size)
{
    void *block = uniform_alloc(size);
    memcpy(block, data, size);
    uniform_commit(block, size);
    GX2SetVertexUniformBlock(location, size, block);
}

void uniform_ps(uint32_t location, const void *data, uint32_t size)
{
    void *block = uniform_alloc(size);
    memcpy(block, data, size);
    uniform_commit(block, size);
    GX2SetPixelUniformBlock(location, size, block);
}

void sampler_linear(GX2Sampler *sampler, GX2TexClampMode clamp, bool mips)
{
    GX2InitSampler(sampler, clamp, GX2_TEX_XY_FILTER_MODE_LINEAR);
    if (mips)
        GX2InitSamplerZMFilter(sampler, GX2_TEX_Z_FILTER_MODE_LINEAR, GX2_TEX_MIP_FILTER_MODE_LINEAR);
}

void sampler_shadow(GX2Sampler *sampler)
{
    GX2InitSampler(sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
    GX2InitSamplerDepthCompare(sampler, GX2_COMPARE_FUNC_LEQUAL);
}

uint32_t gfx_read_pixel(RenderTarget *target, uint32_t x, uint32_t y)
{
    GX2Surface linear;
    const GX2Surface *source = &target->color.surface;
    surface_setup(&linear, source->width, source->height, source->format, GX2_SURFACE_USE_TEXTURE);
    linear.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&linear);
    if (sStagingSize < linear.imageSize) {
        gfx_free(sStaging);
        sStaging = gfx_alloc(linear.imageSize, linear.alignment);
        sStagingSize = sStaging ? linear.imageSize : 0;
        if (!sStaging)
            return 0;
    }
    linear.image = sStaging;
    // Rendering must retire before the copy reads the target, and the copy
    // before the CPU reads the staging surface.
    GX2DrawDone();
    GX2CopySurface(source, 0, 0, &linear, 0, 0);
    GX2DrawDone();
    DCInvalidateRange(sStaging, linear.imageSize);
    const uint8_t *pixel = (const uint8_t *)sStaging + (y * linear.pitch + x) * 4;
    return ((uint32_t)pixel[0] << 24) | ((uint32_t)pixel[1] << 16) | ((uint32_t)pixel[2] << 8) | pixel[3];
}

void fullscreen_draw(void)
{
    GX2SetAttribBuffer(0, 24 * sizeof(float), 4 * sizeof(float), sQuad);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
}
