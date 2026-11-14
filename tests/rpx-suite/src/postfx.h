#pragma once

#include <stdbool.h>

#include "gfx.h"

typedef struct
{
    float threshold, bloomScale, bloomIntensity, exposure, vignette;
} PostParams;

// HDR chain: scene in RGBA16F -> bright pass (half res) -> separable blur
// (horizontal then vertical) -> composite with tonemap + vignette into `out`.
bool postfx_init(void);
void postfx_shutdown(void);
RenderTarget *postfx_scene(void);
void postfx_resolve(RenderTarget *out, const PostParams *params);
