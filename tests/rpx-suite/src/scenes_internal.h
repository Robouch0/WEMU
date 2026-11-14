#pragma once

// Helpers shared by scenes.c and scenes_extra.c.

#include "scenes.h"
#include "world.h"

typedef void (*ProbeDraw)(const Targets *targets, const void *context);

// A deterministic frame whose centre pixel must equal `want` (see scenes.c).
void probe(const Targets *targets, const char *name, ProbeDraw draw, const void *context, const float want[3],
           int tolerance);
void clear_target(RenderTarget *target, float r, float g, float b);
int to_unorm8(float v);

// A world + camera drawn by draw_lighting_probe (shadow pass, clear, lit draw).
typedef struct
{
    World world;
    Object objects[2];
    Camera camera;
} LightingProbe;

void draw_lighting_probe(const Targets *targets, const void *context);
// 40x40 plane facing +z with uv 0..10, centred on the origin.
extern Mesh sProbePlane;

#define DECLARE_SCENE(name)                                                                                            \
    bool name##_init(void);                                                                                            \
    bool name##_render(const Targets *targets, uint32_t frame);                                                        \
    bool name##_check(const Targets *targets);                                                                         \
    void name##_shutdown(void)

DECLARE_SCENE(textures);
DECLARE_SCENE(geometry);
DECLARE_SCENE(instancing);
DECLARE_SCENE(overdraw);
DECLARE_SCENE(branching);
DECLARE_SCENE(loops);
bool endurance_check(const Targets *targets);
