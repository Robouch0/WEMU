#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Shared animated test level: a textured ground plane and a grid of rotating
// spheres, tori and boxes under eight orbiting coloured lights.
bool arena_init(void);
void arena_shutdown(void);

// `side` x `side` objects spaced `spacing` apart (plus the ground when requested).
bool arena_build(World *world, uint32_t side, float spacing, float scale, bool ground);
void arena_free(World *world);
// Deterministic animation for frame `frame`: objects, lights and both cameras.
void arena_animate(World *world, uint32_t frame, float extent, Camera *tv, Camera *drc);
