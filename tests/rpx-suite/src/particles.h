#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "vmath.h"

// Particle simulation split across the three CPU cores: the main thread
// posts a frame message to each worker and waits for all of them before the
// vertex buffer is flushed and drawn.
bool particles_init(uint32_t count);
void particles_shutdown(void);
void particles_update(void);
void particles_draw(const Mat4 *viewProj);

// Re-runs the same number of steps on one thread from the initial state and
// compares positions bit-for-bit plus the cross-core bounce counters.
bool particles_verify(void);
