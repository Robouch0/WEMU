#include "world.h"

#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <string.h>

#include "report.h"
#include "shaders.h"
#include "texture.h"

#define SHADOW_SIZE 1024

// Uniform-block layouts; must match shaders/programs.py.
typedef struct
{
    float mvp[16];
    float model[12];
    float tint[4];
} ObjectBlock;

typedef struct
{
    float lightViewProj[16];
} FrameBlock;

typedef struct
{
    float ambient[4];
    float camera[4];
    float shadow[4]; // u scale, v scale, bias, strength
    float shadowTexel[4];
    struct
    {
        float position[4]; // xyz, 1/r^2
        float color[4];    // rgb, specular
    } lights[WORLD_LIGHTS];
} LightingBlock;

static Program sLit, sLitInstanced, sDepth;
static RenderTarget sShadowMap, sNoShadow;
static GX2Sampler sAlbedoSampler, sPointMipSampler, sShadowSampler;
static GX2Texture sWhite;
static Mat4 sLightViewProj;
static uint32_t sDrawCalls;

bool world_init(void)
{
    if (!program_load(&sLit, SHADER_LIT, MESH_ATTRIBS, 3, sizeof(Vertex)) ||
        !program_load(&sDepth, SHADER_DEPTH, MESH_ATTRIBS, 3, sizeof(Vertex)))
        return false;
    static const AttribDesc instancedAttribs[7] = {
        {"aPosition", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1), 0, false},
        {"aNormal", 12, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_0), 0, false},
        {"aTexCoord", 24, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1), 0, false},
        {"aRow0", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W), 1, true},
        {"aRow1", 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W), 1, true},
        {"aRow2", 32, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W), 1, true},
        {"aTint", 48, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A), 1, true},
    };
    if (!program_load(&sLitInstanced, SHADER_LIT_INSTANCED, instancedAttribs, 7, sizeof(Vertex)))
        return false;
    if (!target_init(&sShadowMap, SHADOW_SIZE, SHADOW_SIZE, 0, true, true) ||
        !target_init(&sNoShadow, 16, 16, 0, true, true))
        return false;
    // A far-plane map: every comparison passes, so unshadowed scenes still sample it.
    GX2ClearDepthStencilEx(&sNoShadow.depth, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    sampler_linear(&sAlbedoSampler, GX2_TEX_CLAMP_MODE_WRAP, true);
    GX2InitSampler(&sPointMipSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);
    GX2InitSamplerZMFilter(&sPointMipSampler, GX2_TEX_Z_FILTER_MODE_POINT, GX2_TEX_MIP_FILTER_MODE_POINT);
    sampler_shadow(&sShadowSampler);
    return texture_solid(&sWhite, 0xFFFFFFFF);
}

void world_shutdown(void)
{
    program_free(&sLit);
    program_free(&sLitInstanced);
    program_free(&sDepth);
    texture_free(&sWhite);
}

const GX2Texture *world_white_texture(void)
{
    return &sWhite;
}

uint32_t world_draw_calls(void)
{
    uint32_t calls = sDrawCalls;
    sDrawCalls = 0;
    return calls;
}

Mat4 camera_view_proj(const Camera *camera)
{
    Mat4 view = m4_look_at(camera->eye, camera->target, v3(0, 1, 0));
    Mat4 proj = m4_perspective(camera->fovY, camera->aspect, camera->zNear, camera->zFar);
    return m4_mul(&proj, &view);
}

static Mat4 light_view_proj(const World *world)
{
    Mat4 view = m4_look_at(world->lights[0].position, world->shadowTarget, v3(0, 1, 0));
    Mat4 proj = m4_perspective(1.4f, 1.0f, 0.5f, 80.0f);
    return m4_mul(&proj, &view);
}

static void object_block(ObjectBlock *block, const Mat4 *viewProj, const Object *object)
{
    Mat4 mvp = m4_mul(viewProj, &object->model);
    memcpy(block->mvp, mvp.m, sizeof(block->mvp));
    memcpy(block->model, object->model.m, sizeof(block->model));
    block->tint[0] = object->tint[0];
    block->tint[1] = object->tint[1];
    block->tint[2] = object->tint[2];
    block->tint[3] = 1.0f;
}

void world_shadow_pass(const World *world)
{
    sLightViewProj = light_view_proj(world);
    GX2ClearDepthStencilEx(&sShadowMap.depth, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    target_bind(&sShadowMap);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, FALSE);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
    // Slope-scaled bias keeps self-shadowing acne out of the map.
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, TRUE, GX2_POLYGON_MODE_TRIANGLE,
                         GX2_POLYGON_MODE_TRIANGLE, TRUE, TRUE, FALSE);
    GX2SetPolygonOffset(2.0f, 2.0f, 2.0f, 2.0f, 0.0f);
    program_bind(&sDepth);
    for (uint32_t i = 0; i < world->objectCount; ++i) {
        ObjectBlock *block = uniform_alloc(sizeof(ObjectBlock));
        object_block(block, &sLightViewProj, &world->objects[i]);
        uniform_commit(block, sizeof(ObjectBlock));
        GX2SetVertexUniformBlock(0, sizeof(ObjectBlock), block);
        mesh_draw(world->objects[i].mesh);
        ++sDrawCalls;
    }
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE,
                         GX2_POLYGON_MODE_TRIANGLE, FALSE, FALSE, FALSE);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, sShadowMap.depth.surface.image, sShadowMap.depth.surface.imageSize);
}

// Lighting/shadow state shared by the lit programs; binds `program`.
static void bind_lighting(const World *world, const Camera *camera, const Program *program)
{
    if (!world->shadows)
        sLightViewProj = light_view_proj(world);

    LightingBlock *lighting = uniform_alloc(sizeof(LightingBlock));
    memset(lighting, 0, sizeof(*lighting));
    memcpy(lighting->ambient, world->ambient, sizeof(world->ambient));
    lighting->camera[0] = camera->eye.x;
    lighting->camera[1] = camera->eye.y;
    lighting->camera[2] = camera->eye.z;
    lighting->shadow[0] = 0.5f;
    lighting->shadow[1] = -0.5f;
    lighting->shadow[2] = 0.0005f;
    lighting->shadow[3] = world->shadows ? world->shadowStrength : 0.0f;
    lighting->shadowTexel[0] = 0.5f / SHADOW_SIZE;
    for (int i = 0; i < WORLD_LIGHTS; ++i) {
        const Light *light = &world->lights[i];
        lighting->lights[i].position[0] = light->position.x;
        lighting->lights[i].position[1] = light->position.y;
        lighting->lights[i].position[2] = light->position.z;
        lighting->lights[i].position[3] = light->radius > 0.0f ? 1.0f / (light->radius * light->radius) : 1e6f;
        memcpy(lighting->lights[i].color, light->color, sizeof(light->color));
        lighting->lights[i].color[3] = light->specular;
    }
    uniform_commit(lighting, sizeof(LightingBlock));

    FrameBlock *frame = uniform_alloc(sizeof(FrameBlock));
    memcpy(frame->lightViewProj, sLightViewProj.m, sizeof(frame->lightViewProj));
    uniform_commit(frame, sizeof(FrameBlock));

    program_bind(program);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
    GX2SetPixelUniformBlock(0, sizeof(LightingBlock), lighting);
    GX2SetVertexUniformBlock(1, sizeof(FrameBlock), frame);
    RenderTarget *shadow = world->shadows ? &sShadowMap : &sNoShadow;
    GX2SetPixelTexture(&shadow->depthTexture, 1);
    GX2SetPixelSampler(&sShadowSampler, 1);
    GX2SetPixelSampler(world->pointMips ? &sPointMipSampler : &sAlbedoSampler, 0);
}

void world_draw(const World *world, const Camera *camera)
{
    Mat4 viewProj = camera_view_proj(camera);
    bind_lighting(world, camera, &sLit);
    const GX2Texture *bound = NULL;
    for (uint32_t i = 0; i < world->objectCount; ++i) {
        const Object *object = &world->objects[i];
        const GX2Texture *albedo = object->albedo ? object->albedo : &sWhite;
        if (albedo != bound) {
            GX2SetPixelTexture(albedo, 0);
            bound = albedo;
        }
        ObjectBlock *block = uniform_alloc(sizeof(ObjectBlock));
        object_block(block, &viewProj, object);
        uniform_commit(block, sizeof(ObjectBlock));
        GX2SetVertexUniformBlock(0, sizeof(ObjectBlock), block);
        mesh_draw(object->mesh);
        ++sDrawCalls;
    }
}

void world_draw_instanced(const World *world, const Camera *camera, const Mesh *mesh, const GX2Texture *albedo,
                          const Instance *instances, uint32_t count)
{
    Mat4 viewProj = camera_view_proj(camera);
    bind_lighting(world, camera, &sLitInstanced);
    uniform_vs(0, viewProj.m, sizeof(viewProj.m));
    GX2SetPixelTexture(albedo ? albedo : &sWhite, 0);
    GX2SetAttribBuffer(0, mesh->vertexCount * sizeof(Vertex), sizeof(Vertex), mesh->vertices);
    GX2SetAttribBuffer(1, count * sizeof(Instance), sizeof(Instance), instances);
    GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES, mesh->indexCount, mesh->wideIndices ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16,
                     mesh->indices, 0, count);
    ++sDrawCalls;
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void world_shade_reference(const World *world, const Object *object, Vec3 point, Vec3 normal, Vec3 eye,
                           const float albedo[3], const bool shadowed[WORLD_LIGHTS], float out[3])
{
    Vec3 n = v3_norm(normal), view = v3_norm(v3_sub(eye, point));
    float diffuse[3] = {world->ambient[0], world->ambient[1], world->ambient[2]}, specular[3] = {0, 0, 0};
    for (int i = 0; i < WORLD_LIGHTS; ++i) {
        const Light *light = &world->lights[i];
        Vec3 l = v3_sub(light->position, point);
        float d2 = v3_dot(l, l);
        float invR2 = light->radius > 0.0f ? 1.0f / (light->radius * light->radius) : 1e6f;
        float atten = clamp01(1.0f - d2 * invR2);
        l = v3_norm(l);
        float ndl = v3_dot(n, l);
        ndl = ndl > 0.0f ? ndl : 0.0f;
        float ndh = v3_dot(n, v3_norm(v3_add(l, view)));
        ndh = ndh > 0.0f ? ndh : 0.0f;
        float spec = ndh;
        for (int k = 0; k < 5; ++k)
            spec *= spec;
        float visible = (i == 0 && shadowed && shadowed[0] && world->shadows) ? 1.0f - world->shadowStrength : 1.0f;
        for (int c = 0; c < 3; ++c) {
            diffuse[c] += light->color[c] * ndl * atten * visible;
            specular[c] += light->color[c] * spec * atten * light->specular * visible;
        }
    }
    for (int c = 0; c < 3; ++c)
        out[c] = clamp01(albedo[c] * object->tint[c] * diffuse[c] + specular[c]);
}
