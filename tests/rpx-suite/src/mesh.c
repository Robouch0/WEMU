#include "mesh.h"

#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/utils.h>
#include <math.h>
#include <string.h>

#include "report.h"

#define XYZ1 GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1)
#define XYZ0 GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_0)
#define XY01 GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1)

const AttribDesc MESH_ATTRIBS[3] = {
    {"aPosition", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, XYZ1},
    {"aNormal", 12, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, XYZ0},
    {"aTexCoord", 24, GX2_ATTRIB_FORMAT_FLOAT_32_32, XY01},
};

static uint32_t index_size(const Mesh *mesh)
{
    return mesh->wideIndices ? sizeof(uint32_t) : sizeof(uint16_t);
}

static bool mesh_alloc_wide(Mesh *mesh, uint32_t vertices, uint32_t indices, bool wide)
{
    memset(mesh, 0, sizeof(*mesh));
    if (!wide && vertices > 65536)
        return report_error("mesh has %u vertices (16-bit indices)", (unsigned)vertices);
    mesh->wideIndices = wide;
    mesh->vertices = gfx_alloc(vertices * sizeof(Vertex), GX2_VERTEX_BUFFER_ALIGNMENT);
    mesh->indices = gfx_alloc(indices * index_size(mesh), GX2_INDEX_BUFFER_ALIGNMENT);
    mesh->vertexCount = vertices;
    mesh->indexCount = indices;
    return mesh->vertices && mesh->indices ? true : report_error("mesh allocation");
}

static bool mesh_alloc(Mesh *mesh, uint32_t vertices, uint32_t indices)
{
    return mesh_alloc_wide(mesh, vertices, indices, false);
}

static void mesh_flush(Mesh *mesh)
{
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, mesh->vertices, mesh->vertexCount * sizeof(Vertex));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, mesh->indices, mesh->indexCount * index_size(mesh));
}

static void set_index(Mesh *mesh, uint32_t position, uint32_t value)
{
    if (mesh->wideIndices)
        ((uint32_t *)mesh->indices)[position] = value;
    else
        ((uint16_t *)mesh->indices)[position] = (uint16_t)value;
}

// Grid of (rings + 1) x (segments + 1) vertices with two triangles per cell.
static void grid_indices(Mesh *mesh, uint32_t rings, uint32_t segments)
{
    uint32_t n = 0;
    for (uint32_t r = 0; r < rings; ++r)
        for (uint32_t s = 0; s < segments; ++s) {
            uint32_t a = r * (segments + 1) + s, b = a + segments + 1;
            const uint32_t quad[6] = {a, b, a + 1, a + 1, b, b + 1};
            for (int i = 0; i < 6; ++i)
                set_index(mesh, n++, quad[i]);
        }
}

static bool sphere(Mesh *mesh, float radius, uint32_t rings, uint32_t segments, bool wide)
{
    if (!mesh_alloc_wide(mesh, (rings + 1) * (segments + 1), rings * segments * 6, wide))
        return false;
    Vertex *v = mesh->vertices;
    for (uint32_t r = 0; r <= rings; ++r) {
        float phi = (float)M_PI * (float)r / (float)rings;
        for (uint32_t s = 0; s <= segments; ++s, ++v) {
            float theta = 2.0f * (float)M_PI * (float)s / (float)segments;
            float nx = sinf(phi) * cosf(theta), ny = cosf(phi), nz = sinf(phi) * sinf(theta);
            *v = (Vertex){{nx * radius, ny * radius, nz * radius}, {nx, ny, nz},
                          {(float)s / (float)segments * 4.0f, (float)r / (float)rings * 2.0f}};
        }
    }
    grid_indices(mesh, rings, segments);
    mesh_flush(mesh);
    return true;
}

bool mesh_sphere(Mesh *mesh, float radius, uint32_t rings, uint32_t segments)
{
    return sphere(mesh, radius, rings, segments, false);
}

bool mesh_sphere_wide(Mesh *mesh, float radius, uint32_t rings, uint32_t segments)
{
    return sphere(mesh, radius, rings, segments, true);
}

bool mesh_torus(Mesh *mesh, float major, float minor, uint32_t rings, uint32_t segments)
{
    if (!mesh_alloc(mesh, (rings + 1) * (segments + 1), rings * segments * 6))
        return false;
    Vertex *v = mesh->vertices;
    for (uint32_t r = 0; r <= rings; ++r) {
        float u = 2.0f * (float)M_PI * (float)r / (float)rings;
        for (uint32_t s = 0; s <= segments; ++s, ++v) {
            float w = 2.0f * (float)M_PI * (float)s / (float)segments;
            float nx = cosf(w) * cosf(u), ny = sinf(w), nz = cosf(w) * sinf(u);
            float ring = major + minor * cosf(w);
            *v = (Vertex){{ring * cosf(u), minor * ny, ring * sinf(u)}, {nx, ny, nz},
                          {(float)r / (float)rings * 8.0f, (float)s / (float)segments * 2.0f}};
        }
    }
    grid_indices(mesh, rings, segments);
    mesh_flush(mesh);
    return true;
}

bool mesh_box(Mesh *mesh, float half)
{
    static const float faces[6][3][3] = {
        // normal, tangent u, tangent v
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    };
    if (!mesh_alloc(mesh, 24, 36))
        return false;
    for (uint32_t f = 0; f < 6; ++f) {
        const float *n = faces[f][0], *tu = faces[f][1], *tv = faces[f][2];
        for (uint32_t c = 0; c < 4; ++c) {
            float su = (c & 1) ? 1.0f : -1.0f, sv = (c & 2) ? 1.0f : -1.0f;
            Vertex *v = &mesh->vertices[f * 4 + c];
            for (int i = 0; i < 3; ++i) {
                v->position[i] = (n[i] + tu[i] * su + tv[i] * sv) * half;
                v->normal[i] = n[i];
            }
            v->uv[0] = (su + 1.0f) * 0.5f;
            v->uv[1] = (1.0f - sv) * 0.5f;
        }
        const uint32_t base = f * 4, order[6] = {0, 1, 2, 2, 1, 3};
        for (int i = 0; i < 6; ++i)
            set_index(mesh, f * 6 + (uint32_t)i, base + order[i]);
    }
    mesh_flush(mesh);
    return true;
}

bool mesh_plane(Mesh *mesh, float half, uint32_t divisions)
{
    if (!mesh_alloc(mesh, (divisions + 1) * (divisions + 1), divisions * divisions * 6))
        return false;
    Vertex *v = mesh->vertices;
    for (uint32_t r = 0; r <= divisions; ++r)
        for (uint32_t s = 0; s <= divisions; ++s, ++v) {
            float fx = (float)s / (float)divisions, fy = (float)r / (float)divisions;
            *v = (Vertex){{(fx * 2.0f - 1.0f) * half, (1.0f - fy * 2.0f) * half, 0.0f}, {0, 0, 1},
                          {fx * half * 0.5f, fy * half * 0.5f}};
        }
    grid_indices(mesh, divisions, divisions);
    mesh_flush(mesh);
    return true;
}

void mesh_free(Mesh *mesh)
{
    gfx_free(mesh->vertices);
    gfx_free(mesh->indices);
    memset(mesh, 0, sizeof(*mesh));
}

void mesh_draw(const Mesh *mesh)
{
    GX2SetAttribBuffer(0, mesh->vertexCount * sizeof(Vertex), sizeof(Vertex), mesh->vertices);
    GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES, mesh->indexCount, mesh->wideIndices ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16,
                     mesh->indices, 0, 1);
}
