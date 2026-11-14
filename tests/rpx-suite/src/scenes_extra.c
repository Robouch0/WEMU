// Heavier scenes: texture formats, geometry load, instancing, fill rate and
// shader control flow. Probes follow the same rules as scenes.c.

#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/utils.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "formats.h"
#include "gfx.h"
#include "report.h"
#include "scenes_internal.h"
#include "shaders.h"
#include "texture.h"

static float clock_seconds(uint32_t frame)
{
    return (float)frame / 60.0f;
}

// Eight orbiting lights at moderate strength, shared by the scenes below.
static void orbit_lights(World *world, uint32_t frame, float radius, float height)
{
    static const float colors[WORLD_LIGHTS][3] = {
        {1.0f, 0.95f, 0.85f}, {1.0f, 0.3f, 0.3f}, {0.3f, 1.0f, 0.4f}, {0.4f, 0.5f, 1.0f},
        {1.0f, 0.8f, 0.3f},   {0.9f, 0.4f, 1.0f}, {0.3f, 0.9f, 1.0f}, {1.0f, 0.6f, 0.2f},
    };
    float t = clock_seconds(frame);
    for (int i = 0; i < WORLD_LIGHTS; ++i) {
        float angle = t * (0.25f + 0.05f * (float)i) + (float)i * 0.785f;
        world->lights[i] = (Light){v3(cosf(angle) * radius, height + (float)(i % 3), sinf(angle) * radius),
                                   radius * 1.6f, {colors[i][0], colors[i][1], colors[i][2]}, 0.5f};
    }
}

// ---------------------------------------------------------------------------
// textures: every format in TEXTURE_FORMATS on its own tile, plus a 2048^2 ground.

static GX2Texture sFormatTextures[16], sGroundTexture;
static Mesh sTile;
static Object sTextureObjects[18];
static World sTextureWorld;

static uint8_t *pattern_rgba(uint32_t size, uint32_t cell, const uint8_t a[4], const uint8_t b[4])
{
    uint8_t *rgba = gfx_alloc(size * size * 4, 64);
    if (!rgba)
        return NULL;
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            const uint8_t *c = (((x / cell) ^ (y / cell)) & 1) ? a : b;
            uint8_t *p = rgba + (y * size + x) * 4;
            uint32_t shade = 160 + (x * 95) / size; // gradient exposes quantisation
            for (int ch = 0; ch < 3; ++ch)
                p[ch] = (uint8_t)(c[ch] * shade / 255);
            p[3] = c[3];
        }
    return rgba;
}

bool textures_init(void)
{
    static const uint8_t warm[4] = {230, 110, 40, 255}, cool[4] = {40, 150, 230, 255};
    uint8_t *rgba = pattern_rgba(256, 32, warm, cool);
    if (!rgba)
        return report_error("pattern allocation");
    bool ok = mesh_plane(&sTile, 2.0f, 1);
    for (uint32_t i = 0; ok && i < TEXTURE_FORMAT_COUNT; ++i)
        ok = texture_from_rgba(&sFormatTextures[i], &TEXTURE_FORMATS[i], 256, 256, rgba);
    gfx_free(rgba);
    static const uint8_t light[4] = {235, 235, 225, 255}, dark[4] = {70, 70, 80, 255};
    uint8_t *ground = ok ? pattern_rgba(2048, 16, light, dark) : NULL;
    ok = ok && ground && texture_from_rgba(&sGroundTexture, &TEXTURE_FORMATS[5], 2048, 2048, ground);
    gfx_free(ground);
    if (!ok)
        return false;

    memset(&sTextureWorld, 0, sizeof(sTextureWorld));
    sTextureWorld.objects = sTextureObjects;
    for (uint32_t i = 0; i < TEXTURE_FORMAT_COUNT; ++i)
        sTextureObjects[i] = (Object){&sTile, &sFormatTextures[i], m4_identity(), {1, 1, 1}};
    Mat4 floor = m4_rotate_x(-1.5707963f);
    sTextureObjects[TEXTURE_FORMAT_COUNT] = (Object){&sProbePlane, &sGroundTexture, floor, {1, 1, 1}};
    sTextureWorld.objectCount = TEXTURE_FORMAT_COUNT + 1;
    sTextureWorld.ambient[0] = sTextureWorld.ambient[1] = sTextureWorld.ambient[2] = 0.45f;
    return true;
}

bool textures_render(const Targets *targets, uint32_t frame)
{
    float t = clock_seconds(frame);
    for (uint32_t i = 0; i < TEXTURE_FORMAT_COUNT; ++i) {
        float x = ((float)(i % 4) - 1.5f) * 2.6f, y = 1.4f + (float)(i / 4) * 2.4f;
        Mat4 turn = m4_rotate_y(sinf(t * 0.7f + (float)i) * 0.5f), scale = m4_scale(0.55f);
        Mat4 move = m4_translate(v3(x, y, 0.0f)), turned = m4_mul(&turn, &scale);
        sTextureObjects[i].model = m4_mul(&move, &turned);
    }
    orbit_lights(&sTextureWorld, frame, 6.0f, 2.0f);
    // Dolly in and out so minification and magnification both occur.
    Camera camera = {v3(sinf(t * 0.2f) * 5.0f, 3.5f, 8.0f + 5.0f * sinf(t * 0.31f)), v3(0, 3.6f, 0), 0.9f,
                     (float)TV_WIDTH / TV_HEIGHT, 0.3f, 100.0f};
    clear_target(targets->tv, 0.03f, 0.03f, 0.05f);
    world_draw(&sTextureWorld, &camera);
    return false;
}

static void solid_probe_world(LightingProbe *p, const GX2Texture *texture)
{
    memset(p, 0, sizeof(*p));
    p->objects[0] = (Object){&sProbePlane, texture, m4_identity(), {1, 1, 1}};
    p->world.objects = p->objects;
    p->world.objectCount = 1;
    p->world.ambient[0] = p->world.ambient[1] = p->world.ambient[2] = 1.0f;
    p->camera = (Camera){v3(0, 0, 10), v3(0, 0, 0), 0.8f, (float)TV_WIDTH / TV_HEIGHT, 0.5f, 50.0f};
}

bool textures_check(const Targets *targets)
{
    static const uint8_t color[4] = {200, 120, 40, 255};
    uint8_t solid[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; ++i)
        memcpy(solid + i * 4, color, 4);
    bool ok = true;
    for (uint32_t i = 0; i < TEXTURE_FORMAT_COUNT; ++i) {
        const TextureFormatInfo *format = &TEXTURE_FORMATS[i];
        GX2Texture texture;
        if (!texture_from_rgba(&texture, format, 16, 16, solid)) {
            ok = false;
            continue;
        }
        static LightingProbe p;
        solid_probe_world(&p, &texture);
        float want[3];
        format_expected(format, color, want);
        char name[32];
        snprintf(name, sizeof(name), "format_%s", format->name);
        probe(targets, name, draw_lighting_probe, &p, want, 3);
        GX2DrawDone();
        gfx_free(texture.surface.image);
    }
    return ok;
}

void textures_shutdown(void)
{
    for (uint32_t i = 0; i < TEXTURE_FORMAT_COUNT; ++i)
        gfx_free(sFormatTextures[i].surface.image);
    gfx_free(sGroundTexture.surface.image);
    mesh_free(&sTile);
}

// ---------------------------------------------------------------------------
// geometry: ~2.4M triangles per frame from dense spheres with 32-bit indices.

static Mesh sDense[3];
static GX2Texture sGeometryTexture;
static Object sGeometryObjects[25];
static World sGeometryWorld;
static uint32_t sTriangles;

bool geometry_init(void)
{
    static const uint8_t a[4] = {210, 210, 200, 255}, b[4] = {90, 110, 150, 255};
    uint8_t *rgba = pattern_rgba(256, 16, a, b);
    bool ok = rgba && texture_from_rgba(&sGeometryTexture, &TEXTURE_FORMATS[5], 256, 256, rgba) &&
              mesh_sphere_wide(&sDense[0], 1.0f, 192, 256) && mesh_sphere_wide(&sDense[1], 1.0f, 128, 192) &&
              mesh_sphere_wide(&sDense[2], 1.0f, 96, 128);
    gfx_free(rgba);
    if (!ok)
        return false;
    memset(&sGeometryWorld, 0, sizeof(sGeometryWorld));
    sGeometryWorld.objects = sGeometryObjects;
    sGeometryWorld.objectCount = 24;
    sTriangles = 0;
    for (uint32_t i = 0; i < 24; ++i) {
        sGeometryObjects[i] = (Object){&sDense[i % 3], &sGeometryTexture, m4_identity(),
                                       {0.7f + 0.3f * (float)(i % 2), 0.8f, 0.7f + 0.3f * (float)(i % 3) / 2.0f}};
        sTriangles += sDense[i % 3].indexCount / 3;
    }
    sGeometryWorld.ambient[0] = sGeometryWorld.ambient[1] = sGeometryWorld.ambient[2] = 0.08f;
    return true;
}

bool geometry_render(const Targets *targets, uint32_t frame)
{
    float t = clock_seconds(frame);
    for (uint32_t i = 0; i < 24; ++i) {
        float x = ((float)(i % 6) - 2.5f) * 2.6f, z = ((float)(i / 6) - 1.5f) * 2.6f;
        Mat4 spin = m4_rotate_y(t * 0.5f + (float)i), move = m4_translate(v3(x, 1.2f + sinf(t + (float)i) * 0.3f, z));
        sGeometryObjects[i].model = m4_mul(&move, &spin);
    }
    orbit_lights(&sGeometryWorld, frame, 8.0f, 3.0f);
    float orbit = t * 0.12f;
    Camera camera = {v3(cosf(orbit) * 13.0f, 7.0f, sinf(orbit) * 13.0f), v3(0, 0.8f, 0), 0.9f,
                     (float)TV_WIDTH / TV_HEIGHT, 0.5f, 80.0f};
    clear_target(targets->tv, 0.02f, 0.02f, 0.04f);
    world_draw(&sGeometryWorld, &camera);
    return false;
}

bool geometry_check(const Targets *targets)
{
    (void)targets;
    report("detail triangles_per_frame=%u draws=%u index=u32", (unsigned)sTriangles,
           (unsigned)sGeometryWorld.objectCount);
    return true;
}

void geometry_shutdown(void)
{
    for (int i = 0; i < 3; ++i)
        mesh_free(&sDense[i]);
    gfx_free(sGeometryTexture.surface.image);
}

// ---------------------------------------------------------------------------
// instancing: 4096 animated boxes in a single draw, per-instance data rebuilt each frame.

#define INSTANCE_SIDE 64
#define INSTANCES (INSTANCE_SIDE * INSTANCE_SIDE)

static Mesh sInstanceBox;
static Instance *sInstances[2];
static uint32_t sInstanceBuffer;
static Object sInstanceGround;
static World sInstanceWorld;

bool instancing_init(void)
{
    for (int b = 0; b < 2; ++b) {
        sInstances[b] = gfx_alloc(INSTANCES * sizeof(Instance), GX2_VERTEX_BUFFER_ALIGNMENT);
        if (!sInstances[b])
            return report_error("instance buffer");
    }
    if (!mesh_box(&sInstanceBox, 0.3f))
        return false;
    memset(&sInstanceWorld, 0, sizeof(sInstanceWorld));
    sInstanceGround = (Object){&sProbePlane, NULL, m4_rotate_x(-1.5707963f), {0.5f, 0.5f, 0.55f}};
    sInstanceWorld.objects = &sInstanceGround;
    sInstanceWorld.objectCount = 1;
    sInstanceWorld.ambient[0] = sInstanceWorld.ambient[1] = sInstanceWorld.ambient[2] = 0.1f;
    return true;
}

static void set_instance(Instance *instance, const Mat4 *model, uint32_t tint)
{
    for (int r = 0; r < 3; ++r)
        memcpy(instance->rows[r], &model->m[r * 4], sizeof(instance->rows[r]));
    instance->tint = tint;
}

bool instancing_render(const Targets *targets, uint32_t frame)
{
    float t = clock_seconds(frame);
    sInstanceBuffer ^= 1;
    Instance *instances = sInstances[sInstanceBuffer];
    for (uint32_t i = 0; i < INSTANCES; ++i) {
        float x = ((float)(i % INSTANCE_SIDE) - INSTANCE_SIDE / 2) * 0.8f;
        float z = ((float)(i / INSTANCE_SIDE) - INSTANCE_SIDE / 2) * 0.8f;
        float wave = sinf(sqrtf(x * x + z * z) * 0.4f - t * 2.0f);
        Mat4 spin = m4_rotate_y(t + (float)i * 0.1f), move = m4_translate(v3(x, 0.6f + wave * 0.8f, z));
        Mat4 model = m4_mul(&move, &spin);
        uint32_t r = (uint32_t)(128 + 120 * wave), g = 90 + (i * 7) % 120, b = 255 - r / 2;
        set_instance(&instances[i], &model, (r << 24) | (g << 16) | (b << 8) | 0xFF);
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, instances, INSTANCES * sizeof(Instance));
    orbit_lights(&sInstanceWorld, frame, 18.0f, 4.0f);
    float orbit = t * 0.1f;
    Camera camera = {v3(cosf(orbit) * 30.0f, 16.0f, sinf(orbit) * 30.0f), v3(0, 0, 0), 0.9f,
                     (float)TV_WIDTH / TV_HEIGHT, 0.5f, 150.0f};
    clear_target(targets->tv, 0.02f, 0.02f, 0.04f);
    world_draw(&sInstanceWorld, &camera);
    world_draw_instanced(&sInstanceWorld, &camera, &sInstanceBox, NULL, instances, INSTANCES);
    return false;
}

typedef struct
{
    LightingProbe lit;
    Instance *instance;
} InstanceProbe;

static void draw_instance_probe(const Targets *targets, const void *context)
{
    const InstanceProbe *p = context;
    world_shadow_pass(&p->lit.world);
    clear_target(targets->tv, 0, 0, 0);
    world_draw_instanced(&p->lit.world, &p->lit.camera, &sProbePlane, NULL, p->instance, 1);
}

// The lit_single_light probe again, drawn as one instance: same expected pixel.
bool instancing_check(const Targets *targets)
{
    static InstanceProbe p;
    memset(&p, 0, sizeof(p));
    p.instance = sInstances[0];
    Mat4 identity = m4_identity();
    set_instance(p.instance, &identity, 0xFFFFFFFF);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, p.instance, sizeof(Instance));
    p.lit.objects[0] = (Object){&sProbePlane, NULL, identity, {1, 1, 1}};
    p.lit.world.objects = p.lit.objects;
    p.lit.world.objectCount = 1;
    p.lit.world.ambient[0] = p.lit.world.ambient[1] = p.lit.world.ambient[2] = 0.1f;
    p.lit.world.shadows = true;
    p.lit.world.shadowStrength = 1.0f;
    p.lit.world.lights[0] = (Light){v3(6.0f, 0.0f, 8.0f), 20.0f, {0.8f, 0.6f, 0.4f}, 0.5f};
    p.lit.camera = (Camera){v3(0, 0, 10), v3(0, 0, 0), 0.8f, (float)TV_WIDTH / TV_HEIGHT, 0.5f, 50.0f};
    const float white[3] = {1, 1, 1};
    const bool unshadowed[WORLD_LIGHTS] = {false};
    float want[3];
    world_shade_reference(&p.lit.world, &p.lit.objects[0], v3(0, 0, 0), v3(0, 0, 1), p.lit.camera.eye, white,
                          unshadowed, want);
    probe(targets, "instanced_lit", draw_instance_probe, &p, want, 4);
    report("detail instances=%u draws_per_frame=2", (unsigned)INSTANCES);
    return true;
}

void instancing_shutdown(void)
{
    mesh_free(&sInstanceBox);
    for (int b = 0; b < 2; ++b)
        gfx_free(sInstances[b]);
}

// ---------------------------------------------------------------------------
// overdraw: 48 full-screen textured layers, alternating alpha and additive blending.

#define LAYERS 48

typedef struct
{
    float position[2], uv[2];
    uint32_t color;
} LayerVertex;

static Program sLayerProgram;
static GX2Texture sLayerTexture, sWhiteLayer;
static GX2Sampler sLayerSampler;
static LayerVertex *sLayerVertices[2];
static uint32_t sLayerBuffer;

static const AttribDesc LAYER_ATTRIBS[3] = {
    {"aPosition", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1), 0, false},
    {"aTexCoord", 8, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1), 0, false},
    {"aColor", 16, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A), 0, false},
};

bool overdraw_init(void)
{
    static const uint8_t a[4] = {255, 255, 255, 255}, b[4] = {120, 120, 140, 255};
    uint8_t *rgba = pattern_rgba(256, 32, a, b);
    uint8_t white[16 * 4];
    memset(white, 0xFF, sizeof(white));
    bool ok = rgba && texture_from_rgba(&sLayerTexture, &TEXTURE_FORMATS[5], 256, 256, rgba) &&
              texture_from_rgba(&sWhiteLayer, &TEXTURE_FORMATS[5], 4, 4, white) &&
              program_load(&sLayerProgram, SHADER_OVERLAY, LAYER_ATTRIBS, 3, sizeof(LayerVertex));
    gfx_free(rgba);
    for (int b2 = 0; ok && b2 < 2; ++b2) {
        sLayerVertices[b2] = gfx_alloc(LAYERS * 6 * sizeof(LayerVertex), GX2_VERTEX_BUFFER_ALIGNMENT);
        ok = sLayerVertices[b2] != NULL;
    }
    sampler_linear(&sLayerSampler, GX2_TEX_CLAMP_MODE_WRAP, false);
    return ok ? true : report_error("overdraw resources");
}

// Writes LAYERS full-screen quads; `colorFor(i)` gives each layer's RGBA8 colour.
static LayerVertex *build_layers(uint32_t frame, uint32_t (*colorFor)(uint32_t), bool scroll)
{
    sLayerBuffer ^= 1;
    LayerVertex *v = sLayerVertices[sLayerBuffer];
    float t = clock_seconds(frame);
    for (uint32_t i = 0; i < LAYERS; ++i) {
        float du = scroll ? sinf(t * 0.3f + (float)i) * 0.5f : 0.0f, dv = scroll ? t * 0.02f * (float)(i % 5) : 0.0f;
        float tiles = 2.0f + (float)(i % 4);
        const float corners[6][4] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}};
        for (int k = 0; k < 6; ++k)
            v[i * 6 + (uint32_t)k] = (LayerVertex){{corners[k][0], corners[k][1]},
                                                   {corners[k][2] * tiles + du, corners[k][3] * tiles * 0.56f + dv},
                                                   colorFor(i)};
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, v, LAYERS * 6 * sizeof(LayerVertex));
    return v;
}

static void draw_layers(const Targets *targets, const LayerVertex *v, const GX2Texture *texture, bool alternate)
{
    target_bind(targets->tv);
    program_bind(&sLayerProgram);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
    GX2SetPixelTexture(texture, 0);
    GX2SetPixelSampler(&sLayerSampler, 0);
    GX2SetAttribBuffer(0, LAYERS * 6 * sizeof(LayerVertex), sizeof(LayerVertex), v);
    for (uint32_t i = 0; i < LAYERS; ++i) {
        bool additive = !alternate || (i & 1);
        GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA,
                           additive ? GX2_BLEND_MODE_ONE : GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD,
                           TRUE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, i * 6, 1);
    }
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
}

static uint32_t layer_color(uint32_t i)
{
    uint32_t r = 90 + (i * 37) % 160, g = 90 + (i * 59) % 160, b = 90 + (i * 83) % 160;
    return (r << 24) | (g << 16) | (b << 8) | 28; // alpha ~0.11
}

bool overdraw_render(const Targets *targets, uint32_t frame)
{
    clear_target(targets->tv, 0.0f, 0.0f, 0.0f);
    draw_layers(targets, build_layers(frame, layer_color, true), &sLayerTexture, true);
    return false;
}

static uint32_t probe_layer_color(uint32_t i)
{
    (void)i;
    return (4u << 24) | (2u << 16) | (1u << 8) | 0xFF; // exact steps of 1/255
}

static void draw_overdraw_probe(const Targets *targets, const void *context)
{
    (void)context;
    clear_target(targets->tv, 0.0f, 0.0f, 0.0f);
    draw_layers(targets, build_layers(0, probe_layer_color, false), &sWhiteLayer, false);
}

bool overdraw_check(const Targets *targets)
{
    // 48 additive layers of (4, 2, 1)/255 over black.
    const float want[3] = {LAYERS * 4 / 255.0f, LAYERS * 2 / 255.0f, LAYERS * 1 / 255.0f};
    probe(targets, "overdraw_additive_48", draw_overdraw_probe, NULL, want, 2);
    report("detail layers=%u fullscreen_pixels_per_frame=%u", (unsigned)LAYERS, (unsigned)(LAYERS * TV_WIDTH * TV_HEIGHT));
    return true;
}

void overdraw_shutdown(void)
{
    program_free(&sLayerProgram);
    gfx_free(sLayerTexture.surface.image);
    gfx_free(sWhiteLayer.surface.image);
    for (int b = 0; b < 2; ++b)
        gfx_free(sLayerVertices[b]);
}

// ---------------------------------------------------------------------------
// branching / loops: full-screen shaders with predicated if/else and a loop.

static Program sBranchProgram, sLoopProgram;

static const float BRANCH_A[4] = {0.9f, 0.2f, 0.2f, 1}, BRANCH_B[4] = {0.2f, 0.8f, 0.3f, 1},
                   BRANCH_C[4] = {0.2f, 0.3f, 0.9f, 1};

static void draw_branch(const Targets *targets, float cellsX, float cellsY, float scrollX, float scrollY)
{
    float block[16] = {cellsX, cellsY, scrollX, scrollY};
    memcpy(block + 4, BRANCH_A, sizeof(BRANCH_A));
    memcpy(block + 8, BRANCH_B, sizeof(BRANCH_B));
    memcpy(block + 12, BRANCH_C, sizeof(BRANCH_C));
    target_bind(targets->tv);
    program_bind(&sBranchProgram);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    uniform_ps(0, block, sizeof(block));
    fullscreen_draw();
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
}

bool branching_init(void)
{
    return program_load(&sBranchProgram, SHADER_BRANCH, FULLSCREEN_ATTRIBS, 2, 16);
}

bool branching_render(const Targets *targets, uint32_t frame)
{
    float t = clock_seconds(frame);
    clear_target(targets->tv, 0, 0, 0);
    draw_branch(targets, 12.0f + 4.0f * sinf(t * 0.3f), 7.0f, t * 0.05f, t * 0.031f);
    return false;
}

typedef struct
{
    float cellsX, cellsY;
} BranchProbe;

static void draw_branch_probe(const Targets *targets, const void *context)
{
    const BranchProbe *p = context;
    clear_target(targets->tv, 0, 0, 0);
    draw_branch(targets, p->cellsX, p->cellsY, 0, 0);
}

bool branching_check(const Targets *targets)
{
    // The centre is uv 0.5: fract(0.5 * 5.2) = 0.6, fract(0.5 * 4.4) = 0.2.
    static const BranchProbe a = {5.2f, 5.2f}, b = {5.2f, 4.4f}, c = {4.4f, 5.2f};
    probe(targets, "branch_then_then", draw_branch_probe, &a, BRANCH_A, 2);
    probe(targets, "branch_then_else", draw_branch_probe, &b, BRANCH_B, 2);
    probe(targets, "branch_else", draw_branch_probe, &c, BRANCH_C, 2);
    return true;
}

void branching_shutdown(void)
{
    program_free(&sBranchProgram);
}

static const float LOOP_OUTSIDE[4] = {0.05f, 0.1f, 0.3f, 1}, LOOP_INSIDE[4] = {1.0f, 0.85f, 0.4f, 1};

typedef struct
{
    float cRe, cIm, scale, centreX, centreY;
} LoopParams;

static void draw_loop(const Targets *targets, const LoopParams *p)
{
    float block[16] = {p->cRe, p->cIm, p->scale, 1.0f / 32.0f, p->centreX, p->centreY, 0, 0};
    memcpy(block + 8, LOOP_OUTSIDE, sizeof(LOOP_OUTSIDE));
    memcpy(block + 12, LOOP_INSIDE, sizeof(LOOP_INSIDE));
    target_bind(targets->tv);
    program_bind(&sLoopProgram);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    uniform_ps(0, block, sizeof(block));
    fullscreen_draw();
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
}

bool loops_init(void)
{
    return program_load(&sLoopProgram, SHADER_LOOP, FULLSCREEN_ATTRIBS, 2, 16);
}

bool loops_render(const Targets *targets, uint32_t frame)
{
    float t = clock_seconds(frame) * 0.25f;
    LoopParams p = {0.7885f * cosf(t), 0.7885f * sinf(t), 3.2f, 0.0f, 0.0f};
    clear_target(targets->tv, 0, 0, 0);
    draw_loop(targets, &p);
    return false;
}

static void draw_loop_probe(const Targets *targets, const void *context)
{
    clear_target(targets->tv, 0, 0, 0);
    draw_loop(targets, context);
}

bool loops_check(const Targets *targets)
{
    // z0 ~ 0 with c = -0.1+0.1i never escapes (32 counted iterations); z0 = 3+3i escapes at once.
    static const LoopParams inside = {-0.1f, 0.1f, 0.01f, 0.0f, 0.0f}, outside = {-0.1f, 0.1f, 0.01f, 3.0f, 3.0f};
    probe(targets, "loop_32_iterations", draw_loop_probe, &inside, LOOP_INSIDE, 2);
    probe(targets, "loop_escape", draw_loop_probe, &outside, LOOP_OUTSIDE, 2);
    return true;
}

void loops_shutdown(void)
{
    program_free(&sLoopProgram);
}
