#include "scenes.h"

#include <gx2/clear.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vpad/input.h>

#include "arena.h"
#include "input.h"
#include "overlay.h"
#include "particles.h"
#include "postfx.h"
#include "report.h"
#include "scenes_internal.h"
#include "texture.h"

static World sWorld;
Mesh sProbePlane;
static Mesh sProbeBox, sMipPlane;
static GX2Texture sMipTexture;

void clear_target(RenderTarget *target, float r, float g, float b)
{
    GX2ClearColor(&target->color, r, g, b, 1.0f);
    GX2ClearDepthStencilEx(&target->depth, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    target_bind(target);
}

int to_unorm8(float v)
{
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return (int)(v * 255.0f + 0.5f);
}

// ---------------------------------------------------------------------------
// Probes: a deterministic frame whose centre pixel is predicted on the CPU.
// The frame is presented PROBE_PRESENTS times and the runner checks the TV
// image it receives. The guest's own GX2CopySurface readback is reported
// alongside as a capability, so a readback gap does not hide shading results.

#define PROBE_PRESENTS 3
#define PROBE_HOLD_FRAMES 240 // interactive default: 4 s per probe, A skips

static bool sInteractive;
static uint32_t sProbesShown, sProbeHold = PROBE_HOLD_FRAMES;

void probes_set_hold(uint32_t frames)
{
    sProbeHold = frames ? frames : PROBE_HOLD_FRAMES;
}

void probes_set_interactive(bool interactive)
{
    sInteractive = interactive;
}

uint32_t probes_shown(void)
{
    uint32_t shown = sProbesShown;
    sProbesShown = 0;
    return shown;
}

static uint32_t rgba8(const float color[3])
{
    return ((uint32_t)to_unorm8(color[0]) << 24) | ((uint32_t)to_unorm8(color[1]) << 16) |
           ((uint32_t)to_unorm8(color[2]) << 8) | 0xFF;
}

// The pixel under test stays visible inside a yellow outline; the swatch is the CPU prediction.
static void probe_hud(const char *name, const float want[3], uint32_t guest, int remaining)
{
    const float cx = TV_WIDTH / 2.0f, cy = TV_HEIGHT / 2.0f;
    overlay_begin();
    overlay_frame(cx - 6, cy - 6, 12, 12, 2, 0xFFE000FF);
    overlay_rect(860, 40, 400, 230, 0x000000C0);
    overlay_text(880, 56, 1.5f, 0xFFFFFFFF, "PROBE %s", name);
    overlay_rect(880, 100, 120, 120, rgba8(want));
    overlay_frame(880, 100, 120, 120, 2, 0xFFFFFFFF);
    overlay_text(1016, 104, 1.0f, 0xFFFFFFFF, "expected\n%d,%d,%d", to_unorm8(want[0]), to_unorm8(want[1]),
                 to_unorm8(want[2]));
    overlay_text(1016, 150, 1.0f, 0xC0C0C0FF, "guest readback\n%u,%u,%u", (unsigned)(guest >> 24),
                 (unsigned)((guest >> 16) & 0xFF), (unsigned)((guest >> 8) & 0xFF));
    overlay_text(880, 234, 1.0f, 0xFFE000FF, "centre (yellow box) should match\nA: next   %ds", remaining / 60 + 1);
}

void probe(const Targets *targets, const char *name, ProbeDraw draw, const void *context,
                  const float want[3], int tolerance)
{
    uint32_t first = gfx_frame_index(), guest = 0;
    int presents = sInteractive ? (int)sProbeHold : PROBE_PRESENTS;
    for (int i = 0; i < presents; ++i) {
        gfx_begin_frame();
        draw(targets, context);
        if (i == 0)
            guest = gfx_read_pixel(targets->tv, TV_WIDTH / 2, TV_HEIGHT / 2);
        if (sInteractive) {
            probe_hud(name, want, guest, presents - i);
            overlay_draw(targets->tv);
            if (input_pressed() & VPAD_BUTTON_A)
                presents = i + 1;
        }
        gfx_present(targets->tv, NULL);
    }
    ++sProbesShown;
    report("probe name=%s present=%u count=%d x=%d y=%d want=%d,%d,%d tol=%d guest=%u,%u,%u", name, (unsigned)first,
           presents, TV_WIDTH / 2, TV_HEIGHT / 2, to_unorm8(want[0]), to_unorm8(want[1]), to_unorm8(want[2]),
           tolerance, (unsigned)(guest >> 24), (unsigned)((guest >> 16) & 0xFF), (unsigned)((guest >> 8) & 0xFF));
}

bool probes_init(void)
{
    return mesh_plane(&sProbePlane, 20.0f, 4) && mesh_box(&sProbeBox, 0.75f) && mesh_plane(&sMipPlane, 2.0f, 1) &&
           texture_mip_levels(&sMipTexture, 256);
}

void probes_shutdown(void)
{
    mesh_free(&sProbePlane);
    mesh_free(&sProbeBox);
    mesh_free(&sMipPlane);
    texture_free(&sMipTexture);
}

void draw_lighting_probe(const Targets *targets, const void *context)
{
    const LightingProbe *p = context;
    world_shadow_pass(&p->world);
    clear_target(targets->tv, 0, 0, 0);
    world_draw(&p->world, &p->camera);
}

// A white plane facing the camera, lit by light 0 from the side and optionally
// by seven more lights; an occluder box can sit between light 0 and the centre.
static void probe_lighting(const Targets *targets, const char *name, bool eightLights, bool occluder)
{
    static LightingProbe p;
    memset(&p, 0, sizeof(p));
    p.objects[0] = (Object){&sProbePlane, NULL, m4_identity(), {1.0f, 1.0f, 1.0f}};
    p.objects[1] = (Object){&sProbeBox, NULL, m4_translate(v3(3.0f, 0.0f, 4.0f)), {1.0f, 1.0f, 1.0f}};
    World *world = &p.world;
    world->objects = p.objects;
    world->objectCount = occluder ? 2 : 1;
    world->ambient[0] = world->ambient[1] = world->ambient[2] = 0.1f;
    world->shadows = true;
    world->shadowStrength = 1.0f;
    world->shadowTarget = v3(0, 0, 0);
    world->lights[0] = (Light){v3(6.0f, 0.0f, 8.0f), 20.0f, {0.8f, 0.6f, 0.4f}, 0.5f};
    if (eightLights) // keep the sum below saturation so every light shows in the result
        world->lights[0] = (Light){v3(6.0f, 0.0f, 8.0f), 20.0f, {0.4f, 0.3f, 0.2f}, 0.3f};
    for (int i = 1; i < WORLD_LIGHTS && eightLights; ++i) {
        float angle = (float)i * 0.9f;
        world->lights[i] = (Light){v3(cosf(angle) * 5.0f, sinf(angle) * 5.0f, 4.0f), 12.0f,
                                   {0.015f * (float)i, 0.08f - 0.008f * (float)i, 0.03f + 0.006f * (float)i}, 0.2f};
    }
    p.camera = (Camera){v3(0, 0, 10), v3(0, 0, 0), 0.8f, (float)TV_WIDTH / TV_HEIGHT, 0.5f, 50.0f};

    const float white[3] = {1, 1, 1};
    bool shadowed[WORLD_LIGHTS] = {occluder};
    float want[3];
    world_shade_reference(world, &p.objects[0], v3(0, 0, 0), v3(0, 0, 1), p.camera.eye, white, shadowed, want);
    probe(targets, name, draw_lighting_probe, &p, want, 4);
}

// A 256^2 texture whose levels are solid colours, drawn 32 px wide with
// nearest-mip sampling: the GPU must pick level 3 (8 texels per pixel).
static void probe_mip_selection(const Targets *targets)
{
    static LightingProbe p;
    memset(&p, 0, sizeof(p));
    float f = 1.0f / tanf(0.4f), distance = 10.0f;
    float half = 16.0f * distance / (f * (TV_HEIGHT / 2.0f)); // 16 px half-size
    Mat4 scale = m4_scale(half / 2.0f);                       // sMipPlane spans +-2 with uv 0..1
    p.objects[0] = (Object){&sMipPlane, &sMipTexture, scale, {1.0f, 1.0f, 1.0f}};
    p.world.objects = p.objects;
    p.world.objectCount = 1;
    p.world.ambient[0] = p.world.ambient[1] = p.world.ambient[2] = 1.0f;
    p.world.pointMips = true;
    p.camera = (Camera){v3(0, 0, distance), v3(0, 0, 0), 0.8f, (float)TV_WIDTH / TV_HEIGHT, 0.5f, 50.0f};
    uint32_t color = MIP_COLORS[3];
    const float want[3] = {(float)(color >> 24) / 255.0f, (float)((color >> 16) & 0xFF) / 255.0f,
                           (float)((color >> 8) & 0xFF) / 255.0f};
    probe(targets, "texture_mip_level3", draw_lighting_probe, &p, want, 4);
}

typedef struct
{
    float value;
    PostParams params;
    bool fromShader;
    LightingProbe lit;
} PostProbe;

// The HDR scene is either cleared to `value` or drawn by the lit shader as a
// full-screen white plane with ambient `value`, so clears and shader exports
// are checked separately.
static void draw_post_probe(const Targets *targets, const void *context)
{
    const PostProbe *p = context;
    RenderTarget *scene = postfx_scene();
    if (p->fromShader) {
        clear_target(scene, 0, 0, 0);
        world_draw(&p->lit.world, &p->lit.camera);
    } else {
        GX2ClearColor(&scene->color, p->value, p->value, p->value, 1.0f);
        GX2ClearDepthStencilEx(&scene->depth, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }
    postfx_resolve(targets->tv, &p->params);
}

// A constant HDR image through the post chain; blur of a constant is the constant.
static void probe_post(const Targets *targets, const char *name, float value, const PostParams *params, bool fromShader)
{
    static PostProbe p;
    memset(&p, 0, sizeof(p));
    p.value = value;
    p.params = *params;
    p.fromShader = fromShader;
    p.lit.objects[0] = (Object){&sProbePlane, NULL, m4_identity(), {1.0f, 1.0f, 1.0f}};
    p.lit.world.objects = p.lit.objects;
    p.lit.world.objectCount = 1;
    p.lit.world.ambient[0] = p.lit.world.ambient[1] = p.lit.world.ambient[2] = value;
    p.lit.camera = (Camera){v3(0, 0, 10), v3(0, 0, 0), 0.8f, (float)TV_WIDTH / TV_HEIGHT, 0.5f, 50.0f};
    float lum = value; // grey input; the luminance weights sum to 1
    float excess = lum - params->threshold;
    float bright = value * (excess > 0.0f ? excess : 0.0f) / (lum + 0.0001f) * params->bloomScale;
    float x = (value + bright * params->bloomIntensity) * params->exposure;
    float tone = x / (1.0f + x);
    const float want[3] = {tone, tone, tone};
    probe(targets, name, draw_post_probe, &p, want, 3);
}

// ---------------------------------------------------------------------------
// forward_lit: 64 textured objects + ground, 8 per-pixel lights, TV + top-down DRC view.

static bool forward_init(void)
{
    return arena_build(&sWorld, 8, 3.0f, 0.9f, true);
}

static bool forward_render(const Targets *targets, uint32_t frame)
{
    Camera tv, drc;
    arena_animate(&sWorld, frame, 16.0f, &tv, &drc);
    clear_target(targets->tv, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &tv);
    clear_target(targets->drc, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &drc);
    return true;
}

static bool forward_check(const Targets *targets)
{
    probe_lighting(targets, "lit_single_light", false, false);
    probe_lighting(targets, "lit_eight_lights", true, false);
    probe_mip_selection(targets);
    return true;
}

static void world_scene_shutdown(void)
{
    arena_free(&sWorld);
}

// ---------------------------------------------------------------------------
// shadows: depth-only pass from light 0 into a sampled 1024^2 map, 4-tap PCF.

static bool shadows_init(void)
{
    if (!arena_build(&sWorld, 4, 4.5f, 1.4f, true))
        return false;
    sWorld.shadows = true;
    return true;
}

static bool shadows_render(const Targets *targets, uint32_t frame)
{
    Camera tv;
    arena_animate(&sWorld, frame, 16.0f, &tv, NULL);
    world_shadow_pass(&sWorld);
    clear_target(targets->tv, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &tv);
    return false;
}

static bool shadows_check(const Targets *targets)
{
    probe_lighting(targets, "shadow_unoccluded", false, false);
    probe_lighting(targets, "shadow_occluded", false, true);
    return true;
}

// ---------------------------------------------------------------------------
// draw_calls: 1024 small objects, one uniform block + draw each (submission cost).

static uint32_t sDrawsPerFrame;

static bool draws_init(void)
{
    sDrawsPerFrame = 0;
    return arena_build(&sWorld, 32, 1.2f, 0.35f, true);
}

static bool draws_render(const Targets *targets, uint32_t frame)
{
    Camera tv;
    arena_animate(&sWorld, frame, 24.0f, &tv, NULL);
    clear_target(targets->tv, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &tv);
    sDrawsPerFrame = world_draw_calls();
    return false;
}

static bool draws_check(const Targets *targets)
{
    (void)targets;
    report("detail draws_per_frame=%u expected=%u", (unsigned)sDrawsPerFrame, (unsigned)sWorld.objectCount);
    return sDrawsPerFrame == sWorld.objectCount;
}

// ---------------------------------------------------------------------------
// postfx: HDR (RGBA16F) scene, bright pass, two blur passes, tonemapped composite.

static const PostParams POST_DEFAULT = {1.0f, 0.8f, 0.9f, 1.2f, 0.9f};

static bool postfx_scene_init(void)
{
    return postfx_init() && arena_build(&sWorld, 6, 3.5f, 1.0f, true);
}

static void boost_lights(float factor)
{
    for (int i = 0; i < WORLD_LIGHTS; ++i)
        for (int c = 0; c < 3; ++c)
            sWorld.lights[i].color[c] *= factor;
}

static bool postfx_render(const Targets *targets, uint32_t frame)
{
    Camera tv;
    arena_animate(&sWorld, frame, 16.0f, &tv, NULL);
    boost_lights(2.5f);
    RenderTarget *scene = postfx_scene();
    clear_target(scene, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &tv);
    postfx_resolve(targets->tv, &POST_DEFAULT);
    return false;
}

static bool postfx_check(const Targets *targets)
{
    const PostParams noBloom = {0.9f, 1.0f, 1.0f, 1.0f, 0.0f};
    const PostParams bloom = {0.2f, 1.0f, 1.0f, 1.0f, 0.0f};
    const PostParams hdr = {1.0f, 0.5f, 0.8f, 0.5f, 0.9f};
    probe_post(targets, "post_tonemap", 0.5f, &noBloom, false);
    probe_post(targets, "post_bloom", 0.5f, &bloom, false);
    probe_post(targets, "post_hdr_clear", 2.0f, &hdr, false);
    probe_post(targets, "post_hdr_shader", 2.0f, &hdr, true);
    return true;
}

static void postfx_scene_shutdown(void)
{
    arena_free(&sWorld);
    postfx_shutdown();
}

// ---------------------------------------------------------------------------
// cpu_jobs: 12288 particles simulated on three cores into a dynamic vertex buffer.

static bool jobs_init(void)
{
    return arena_build(&sWorld, 0, 1.0f, 1.0f, true) && particles_init(12288);
}

static bool jobs_render(const Targets *targets, uint32_t frame)
{
    Camera tv;
    arena_animate(&sWorld, frame, 12.0f, &tv, NULL);
    tv.target = v3(0, 2, 0);
    particles_update();
    clear_target(targets->tv, 0.01f, 0.01f, 0.02f);
    world_draw(&sWorld, &tv);
    Mat4 viewProj = camera_view_proj(&tv);
    particles_draw(&viewProj);
    return false;
}

static bool jobs_check(const Targets *targets)
{
    (void)targets;
    return particles_verify();
}

static void jobs_shutdown(void)
{
    particles_shutdown();
    arena_free(&sWorld);
}

// ---------------------------------------------------------------------------
// stress: shadows + 100 objects + particles + HDR post on TV, separate DRC view.

static bool stress_init(void)
{
    if (!postfx_init() || !arena_build(&sWorld, 10, 2.6f, 0.8f, true) || !particles_init(6144))
        return false;
    sWorld.shadows = true;
    return true;
}

static bool stress_render(const Targets *targets, uint32_t frame)
{
    Camera tv, drc;
    arena_animate(&sWorld, frame, 18.0f, &tv, &drc);
    boost_lights(2.0f);
    particles_update();
    world_shadow_pass(&sWorld);
    RenderTarget *scene = postfx_scene();
    clear_target(scene, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &tv);
    Mat4 viewProj = camera_view_proj(&tv);
    particles_draw(&viewProj);
    postfx_resolve(targets->tv, &POST_DEFAULT);
    clear_target(targets->drc, 0.02f, 0.02f, 0.04f);
    world_draw(&sWorld, &drc);
    return true;
}

static bool stress_check(const Targets *targets)
{
    (void)targets;
    return particles_verify();
}

static void stress_shutdown(void)
{
    particles_shutdown();
    arena_free(&sWorld);
    postfx_shutdown();
}

// endurance: the stress load held for a minute of frames; judged by stability and
// memory in the runner (the particle replay would take too long here).
bool endurance_check(const Targets *targets)
{
    (void)targets;
    report("detail endurance=stress-load");
    return true;
}

#define SCENE(name, summary, frames) {#name, summary, frames, name##_init, name##_render, name##_check, name##_shutdown}

const Scene SCENES[] = {
    {"forward_lit", "64 objects, 8 per-pixel lights, TV + DRC views", 600, forward_init, forward_render,
     forward_check, world_scene_shutdown},
    SCENE(textures, "11 texture formats (BC1/3/4/5, packed 16-bit, 10:10:10:2) + 2048^2 ground", 600),
    {"shadows", "depth-only shadow pass + 4-tap PCF", 600, shadows_init, shadows_render, shadows_check,
     world_scene_shutdown},
    SCENE(geometry, "~2.4M triangles/frame, dense meshes, 32-bit indices", 600),
    {"draw_calls", "1025 draws per frame, one uniform block each", 600, draws_init, draws_render, draws_check,
     world_scene_shutdown},
    SCENE(instancing, "4096 animated instances in one draw (per-instance attributes)", 600),
    SCENE(overdraw, "48 full-screen blended layers (fill rate, blend state changes)", 600),
    {"postfx", "RGBA16F scene, bloom (bright + 2 blur passes), tonemap", 600, postfx_scene_init, postfx_render,
     postfx_check, postfx_scene_shutdown},
    SCENE(branching, "nested if/else through the predicate stack", 600),
    SCENE(loops, "Julia set in a 32-iteration shader loop", 300),
    {"cpu_jobs", "12288 particles on 3 cores, dynamic vertex buffer", 600, jobs_init, jobs_render, jobs_check,
     jobs_shutdown},
    {"stress", "shadows + 100 objects + particles + post + DRC", 600, stress_init, stress_render, stress_check,
     stress_shutdown},
    {"endurance", "stress load sustained for 3600 frames (stability, memory)", 3600, stress_init, stress_render,
     endurance_check, stress_shutdown},
};
const uint32_t SCENE_COUNT = sizeof(SCENES) / sizeof(SCENES[0]);
