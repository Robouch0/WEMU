#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

// Interleaved vertex consumed by the lit and depth programs (stride 32).
typedef struct
{
    float position[3];
    float normal[3];
    float uv[2];
} Vertex;

typedef struct
{
    Vertex *vertices;
    void *indices; // uint16_t, or uint32_t when wideIndices
    uint32_t vertexCount, indexCount;
    bool wideIndices;
} Mesh;

extern const AttribDesc MESH_ATTRIBS[3];

bool mesh_sphere(Mesh *mesh, float radius, uint32_t rings, uint32_t segments);
// Same sphere with 32-bit indices (any vertex count).
bool mesh_sphere_wide(Mesh *mesh, float radius, uint32_t rings, uint32_t segments);
bool mesh_torus(Mesh *mesh, float major, float minor, uint32_t rings, uint32_t segments);
bool mesh_box(Mesh *mesh, float half);
// Plane facing +z, centred on the origin.
bool mesh_plane(Mesh *mesh, float half, uint32_t divisions);
void mesh_free(Mesh *mesh);
void mesh_draw(const Mesh *mesh);
