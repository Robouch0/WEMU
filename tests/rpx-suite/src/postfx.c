#include "postfx.h"

#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/texture.h>
#include <string.h>

#include "report.h"
#include "shaders.h"

static RenderTarget sScene, sBloomA, sBloomB;
static Program sBright, sBlur, sComposite;
static GX2Sampler sClamp;
static bool sTargetsReady;

bool postfx_init(void)
{
    // Targets live for the whole run (MEM1 is a frame heap); programs follow the scene.
    if (!sTargetsReady && (!target_init(&sScene, TV_WIDTH, TV_HEIGHT, GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16, true, false) ||
        !target_init(&sBloomA, TV_WIDTH / 2, TV_HEIGHT / 2, GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16, false, false) ||
        !target_init(&sBloomB, TV_WIDTH / 2, TV_HEIGHT / 2, GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16, false, false)))
        return false;
    sTargetsReady = true;
    if (!program_load(&sBright, SHADER_BRIGHT, FULLSCREEN_ATTRIBS, 2, 16) ||
        !program_load(&sBlur, SHADER_BLUR, FULLSCREEN_ATTRIBS, 2, 16) ||
        !program_load(&sComposite, SHADER_COMPOSITE, FULLSCREEN_ATTRIBS, 2, 16))
        return false;
    sampler_linear(&sClamp, GX2_TEX_CLAMP_MODE_CLAMP, false);
    return true;
}

void postfx_shutdown(void)
{
    program_free(&sBright);
    program_free(&sBlur);
    program_free(&sComposite);
}

RenderTarget *postfx_scene(void)
{
    return &sScene;
}

static void finish_target(RenderTarget *target)
{
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE, target->color.surface.image,
                  target->color.surface.imageSize);
}

static void blur_pass(RenderTarget *source, RenderTarget *dest, float stepX, float stepY)
{
    // 9-tap binomial weights (sum 1).
    float block[16] = {stepX, stepY, 0, 0, 1 / 256.f, 8 / 256.f, 28 / 256.f, 56 / 256.f,
                       70 / 256.f, 56 / 256.f, 28 / 256.f, 8 / 256.f, 1 / 256.f, 0, 0, 0};
    target_bind(dest);
    GX2SetPixelTexture(&source->colorTexture, 0);
    uniform_ps(0, block, sizeof(block));
    fullscreen_draw();
    finish_target(dest);
}

void postfx_resolve(RenderTarget *out, const PostParams *params)
{
    finish_target(&sScene);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2SetPixelSampler(&sClamp, 0);
    GX2SetPixelSampler(&sClamp, 1);

    program_bind(&sBright);
    float bright[8] = {0.2126f, 0.7152f, 0.0722f, 0, params->threshold, params->bloomScale, 0, 0};
    target_bind(&sBloomA);
    GX2SetPixelTexture(&sScene.colorTexture, 0);
    uniform_ps(0, bright, sizeof(bright));
    fullscreen_draw();
    finish_target(&sBloomA);

    program_bind(&sBlur);
    blur_pass(&sBloomA, &sBloomB, 1.0f / (TV_WIDTH / 2), 0.0f);
    blur_pass(&sBloomB, &sBloomA, 0.0f, 1.0f / (TV_HEIGHT / 2));

    program_bind(&sComposite);
    float composite[4] = {params->bloomIntensity, params->exposure, params->vignette, 0};
    target_bind(out);
    GX2SetPixelTexture(&sScene.colorTexture, 0);
    GX2SetPixelTexture(&sBloomA.colorTexture, 1);
    uniform_ps(0, composite, sizeof(composite));
    fullscreen_draw();
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
}
