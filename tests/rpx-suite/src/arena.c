#include "arena.h"

#include <string.h>

#include "report.h"
#include "texture.h"

static Mesh sSphere, sTorus, sBox, sGround;
static GX2Texture sTextures[3];

bool arena_init(void)
{
    return mesh_sphere(&sSphere, 1.0f, 24, 32) && mesh_torus(&sTorus, 0.8f, 0.3f, 32, 16) && mesh_box(&sBox, 0.75f) &&
           mesh_plane(&sGround, 40.0f, 32) && texture_checker(&sTextures[0], 256, 0xE0E0E0FF, 0x606060FF, 8) &&
           texture_checker(&sTextures[1], 256, 0xF0B060FF, 0x904020FF, 4) &&
           texture_checker(&sTextures[2], 128, 0x80C0F0FF, 0x305080FF, 16);
}

void arena_shutdown(void)
{
    mesh_free(&sSphere);
    mesh_free(&sTorus);
    mesh_free(&sBox);
    mesh_free(&sGround);
    for (int i = 0; i < 3; ++i)
        texture_free(&sTextures[i]);
}

bool arena_build(World *world, uint32_t side, float spacing, float scale, bool ground)
{
    memset(world, 0, sizeof(*world));
    uint32_t count = side * side + (ground ? 1 : 0);
    world->objects = gfx_alloc(count * sizeof(Object), 16);
    if (!world->objects)
        return report_error("arena objects (%u)", (unsigned)count);
    world->objectCount = count;
    const Mesh *meshes[3] = {&sSphere, &sTorus, &sBox};
    for (uint32_t i = 0; i < side * side; ++i) {
        Object *object = &world->objects[i];
        object->mesh = meshes[i % 3];
        object->albedo = &sTextures[(i / 3) % 3];
        object->tint[0] = 0.6f + 0.4f * (float)((i * 7) % 5) / 4.0f;
        object->tint[1] = 0.6f + 0.4f * (float)((i * 3) % 4) / 3.0f;
        object->tint[2] = 0.6f + 0.4f * (float)((i * 5) % 3) / 2.0f;
        float x = ((float)(i % side) - (float)(side - 1) * 0.5f) * spacing;
        float z = ((float)(i / side) - (float)(side - 1) * 0.5f) * spacing;
        Mat4 s = m4_scale(scale), t = m4_translate(v3(x, 1.2f * scale, z));
        object->model = m4_mul(&t, &s);
    }
    if (ground) {
        Object *floor = &world->objects[count - 1];
        floor->mesh = &sGround;
        floor->albedo = &sTextures[0];
        floor->tint[0] = floor->tint[1] = floor->tint[2] = 0.8f;
        floor->model = m4_rotate_x(-1.5707963f);
    }
    world->ambient[0] = 0.06f;
    world->ambient[1] = 0.06f;
    world->ambient[2] = 0.08f;
    world->shadowStrength = 0.85f;
    world->shadowTarget = v3(0, 0, 0);
    return true;
}

void arena_free(World *world)
{
    gfx_free(world->objects);
    memset(world, 0, sizeof(*world));
}

void arena_animate(World *world, uint32_t frame, float extent, Camera *tv, Camera *drc)
{
    float time = (float)frame / 60.0f;
    uint32_t grid = world->objectCount - (world->objects[world->objectCount - 1].mesh == &sGround ? 1 : 0);
    for (uint32_t i = 0; i < grid; ++i) {
        Object *object = &world->objects[i];
        // Keep translation and scale, replace the rotation.
        float scale = object->model.m[0] * object->model.m[0] + object->model.m[4] * object->model.m[4] +
                      object->model.m[8] * object->model.m[8];
        scale = sqrtf(scale);
        Vec3 position = v3(object->model.m[3], object->model.m[7], object->model.m[11]);
        Mat4 ry = m4_rotate_y(time * (0.6f + 0.05f * (float)(i % 7)) + (float)i);
        Mat4 rx = m4_rotate_x(time * 0.4f + (float)i * 0.3f);
        Mat4 r = m4_mul(&ry, &rx), s = m4_scale(scale), t = m4_translate(position);
        Mat4 rs = m4_mul(&r, &s);
        object->model = m4_mul(&t, &rs);
    }
    static const float colors[WORLD_LIGHTS][3] = {
        {1.0f, 0.95f, 0.85f}, {1.0f, 0.2f, 0.2f}, {0.2f, 1.0f, 0.3f}, {0.3f, 0.4f, 1.0f},
        {1.0f, 0.8f, 0.2f},   {0.9f, 0.3f, 1.0f}, {0.2f, 0.9f, 1.0f}, {1.0f, 0.5f, 0.1f},
    };
    for (int i = 0; i < WORLD_LIGHTS; ++i) {
        Light *light = &world->lights[i];
        float angle = time * (0.3f + 0.07f * (float)i) + (float)i * 0.785f;
        float radius = i == 0 ? extent * 0.35f : extent * (0.2f + 0.05f * (float)i);
        light->position = v3(cosf(angle) * radius, i == 0 ? extent * 0.6f : 2.5f + (float)(i % 3), sinf(angle) * radius);
        light->radius = i == 0 ? extent * 2.0f : extent * 0.6f;
        memcpy(light->color, colors[i], sizeof(light->color));
        light->specular = 0.6f;
    }
    float orbit = time * 0.15f;
    *tv = (Camera){v3(cosf(orbit) * extent, extent * 0.55f, sinf(orbit) * extent), v3(0, 0, 0), 0.9f,
                   (float)TV_WIDTH / TV_HEIGHT, 0.5f, extent * 4.0f};
    if (drc)
        *drc = (Camera){v3(0.0f, extent * 1.4f, extent * 0.3f), v3(0, 0, 0), 0.8f, (float)DRC_WIDTH / DRC_HEIGHT, 1.0f,
                        extent * 4.0f};
}
