#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "mesh.h"
#include "vmath.h"

#define WORLD_LIGHTS 8

typedef struct
{
    const Mesh *mesh;
    const GX2Texture *albedo;
    Mat4 model;
    float tint[3];
} Object;

typedef struct
{
    Vec3 position;
    float radius;
    float color[3];
    float specular;
} Light;

typedef struct
{
    Object *objects;
    uint32_t objectCount;
    Light lights[WORLD_LIGHTS];
    float ambient[3];
    // Light 0 casts shadows when enabled; the map looks from it towards shadowTarget.
    bool shadows;
    float shadowStrength;
    Vec3 shadowTarget;
    // Nearest-mip sampling so a probe can name the exact level the GPU selected.
    bool pointMips;
} World;

typedef struct
{
    Vec3 eye, target;
    float fovY, aspect, zNear, zFar;
} Camera;

Mat4 camera_view_proj(const Camera *camera);

bool world_init(void);
void world_shutdown(void);

// Renders light 0's shadow map (depth-only pass over every object).
void world_shadow_pass(const World *world);
// Draws every object with the lit program into the bound target.
void world_draw(const World *world, const Camera *camera);

// Per-instance data for world_draw_instanced (attribute buffer 1).
typedef struct
{
    float rows[3][4]; // model matrix rows
    uint32_t tint;    // RGBA8
} Instance;

// One instanced draw of `mesh` with the lit pixel shader and the world's lights.
void world_draw_instanced(const World *world, const Camera *camera, const Mesh *mesh, const GX2Texture *albedo,
                          const Instance *instances, uint32_t count);

// CPU evaluation of the lit pixel shader at a surface point (linear 0..1).
void world_shade_reference(const World *world, const Object *object, Vec3 point, Vec3 normal, Vec3 eye,
                           const float albedo[3], const bool shadowed[WORLD_LIGHTS], float out[3]);

const GX2Texture *world_white_texture(void);
uint32_t world_draw_calls(void);
