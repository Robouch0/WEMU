#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

typedef struct
{
    RenderTarget *tv, *drc;
} Targets;

typedef struct
{
    const char *name;
    const char *summary;
    uint32_t frames;
    bool (*init)(void);
    // Renders one frame into the targets; returns true when the DRC target was drawn.
    bool (*render)(const Targets *targets, uint32_t frame);
    // Runs once after the timed frames; self-checks that need no reference image.
    bool (*check)(const Targets *targets);
    void (*shutdown)(void);
} Scene;

extern const Scene SCENES[];
extern const uint32_t SCENE_COUNT;

bool probes_init(void);
// Interactive probes stay on screen with the expected colour beside them
// instead of being presented briefly for the runner.
void probes_set_interactive(bool interactive);
// Frames each interactive probe stays on screen (A skips).
void probes_set_hold(uint32_t frames);
uint32_t probes_shown(void);
void probes_shutdown(void);
