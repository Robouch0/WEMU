#pragma once

#include <math.h>

// Row-major 4x4 matrices applied to column vectors (clip = M * v); rows are
// uploaded as-is and consumed by DOT4 in the vertex shaders.
typedef struct
{
    float x, y, z;
} Vec3;

typedef struct
{
    float m[16];
} Mat4;

static inline Vec3 v3(float x, float y, float z)
{
    return (Vec3){x, y, z};
}

static inline Vec3 v3_add(Vec3 a, Vec3 b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3 v3_sub(Vec3 a, Vec3 b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline Vec3 v3_scale(Vec3 a, float s)
{
    return v3(a.x * s, a.y * s, a.z * s);
}

static inline float v3_dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 v3_cross(Vec3 a, Vec3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline Vec3 v3_norm(Vec3 a)
{
    float length = sqrtf(v3_dot(a, a));
    return length > 0.0f ? v3_scale(a, 1.0f / length) : a;
}

static inline Mat4 m4_identity(void)
{
    return (Mat4){{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}

static inline Mat4 m4_mul(const Mat4 *a, const Mat4 *b)
{
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r.m[row * 4 + col] = a->m[row * 4] * b->m[col] + a->m[row * 4 + 1] * b->m[4 + col] +
                                 a->m[row * 4 + 2] * b->m[8 + col] + a->m[row * 4 + 3] * b->m[12 + col];
    return r;
}

static inline Mat4 m4_translate(Vec3 t)
{
    Mat4 r = m4_identity();
    r.m[3] = t.x;
    r.m[7] = t.y;
    r.m[11] = t.z;
    return r;
}

static inline Mat4 m4_scale(float s)
{
    return (Mat4){{s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1}};
}

static inline Mat4 m4_rotate_y(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    return (Mat4){{c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1}};
}

static inline Mat4 m4_rotate_x(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    return (Mat4){{1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0, 0, 0, 0, 1}};
}

// OpenGL-style projection: clip z in [-1, 1], matching GX2's default clip control.
static inline Mat4 m4_perspective(float fovY, float aspect, float zNear, float zFar)
{
    float f = 1.0f / tanf(fovY * 0.5f);
    return (Mat4){{f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zFar + zNear) / (zNear - zFar),
                   2.0f * zFar * zNear / (zNear - zFar), 0, 0, -1, 0}};
}

static inline Mat4 m4_look_at(Vec3 eye, Vec3 target, Vec3 up)
{
    Vec3 f = v3_norm(v3_sub(target, eye));
    Vec3 s = v3_norm(v3_cross(f, up));
    Vec3 u = v3_cross(s, f);
    return (Mat4){{s.x, s.y, s.z, -v3_dot(s, eye), u.x, u.y, u.z, -v3_dot(u, eye), -f.x, -f.y, -f.z, v3_dot(f, eye),
                   0, 0, 0, 1}};
}
