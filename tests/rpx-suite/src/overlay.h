#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

// On-screen text and rectangles for the interactive mode, in virtual 1280x720
// pixels. Calls after overlay_begin form one batch; overlay_draw can draw it
// into several targets (8x16 font).
bool overlay_init(void);
void overlay_shutdown(void);
void overlay_begin(void);
void overlay_text(float x, float y, float scale, uint32_t rgba, const char *format, ...)
    __attribute__((format(printf, 5, 6)));
void overlay_rect(float x, float y, float width, float height, uint32_t rgba);
// Outline only, so the pixels inside stay visible.
void overlay_frame(float x, float y, float width, float height, float thickness, uint32_t rgba);
void overlay_draw(RenderTarget *target);
