#include "particles.h"

#include <coreinit/atomic.h>
#include <coreinit/cache.h>
#include <coreinit/messagequeue.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/utils.h>
#include <string.h>

#include "gfx.h"
#include "report.h"
#include "shaders.h"

#define WORKERS 3
#define STACK_SIZE (64 * 1024)
#define MSG_STEP 1
#define MSG_QUIT 2

typedef struct
{
    float position[3], velocity[3];
    uint32_t seed, age, life, color;
} Particle;

typedef struct
{
    float position[3];
    uint32_t color;
} ParticleVertex;

typedef struct
{
    OSThread thread;
    OSMessageQueue queue;
    OSMessage messages[4];
    uint8_t *stack;
    uint32_t first, count, bounces;
} Worker;

static Particle *sParticles, *sInitial;
static ParticleVertex *sVertices[2];
static uint32_t sCount, sSteps, sBuffer;
static Worker sWorkers[WORKERS];
static OSMessageQueue sDone;
static OSMessage sDoneMessages[8];
static OSMutex sMutex;
static uint32_t sMutexBounces;
static volatile int32_t sAtomicBounces;
static Program sProgram;

static const AttribDesc PARTICLE_ATTRIBS[2] = {
    {"aPosition", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1)},
    {"aColor", 12, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A)},
};

static uint32_t xorshift(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

static float random_unit(uint32_t *state)
{
    return (float)(xorshift(state) & 0xFFFF) / 65535.0f;
}

static void respawn(Particle *p)
{
    float angle = random_unit(&p->seed) * 6.2831853f, speed = 2.0f + random_unit(&p->seed) * 4.0f;
    p->position[0] = (random_unit(&p->seed) - 0.5f) * 2.0f;
    p->position[1] = 0.5f;
    p->position[2] = (random_unit(&p->seed) - 0.5f) * 2.0f;
    p->velocity[0] = cosf(angle) * speed;
    p->velocity[1] = 6.0f + random_unit(&p->seed) * 6.0f;
    p->velocity[2] = sinf(angle) * speed;
    p->age = 0;
    p->life = 120 + (xorshift(&p->seed) % 240);
    uint32_t hue = xorshift(&p->seed);
    p->color = ((128 + (hue & 127)) << 24) | ((64 + ((hue >> 8) & 127)) << 16) | ((32 + ((hue >> 16) & 63)) << 8) | 0xC0;
}

// One fixed 1/60 s step; returns 1 when the particle bounced off the floor.
static uint32_t step(Particle *p)
{
    const float dt = 1.0f / 60.0f;
    uint32_t bounced = 0;
    p->velocity[1] -= 9.8f * dt;
    for (int i = 0; i < 3; ++i)
        p->position[i] += p->velocity[i] * dt;
    if (p->position[1] < 0.0f) {
        p->position[1] = -p->position[1] * 0.5f;
        p->velocity[1] = -p->velocity[1] * 0.6f;
        p->velocity[0] *= 0.9f;
        p->velocity[2] *= 0.9f;
        bounced = 1;
    }
    if (++p->age > p->life)
        respawn(p);
    return bounced;
}

static void emit(const Particle *p, ParticleVertex *v)
{
    const float s = 0.06f;
    static const float offsets[3][2] = {{-1, -1}, {1, -1}, {0, 1}};
    for (int k = 0; k < 3; ++k) {
        v[k].position[0] = p->position[0] + offsets[k][0] * s;
        v[k].position[1] = p->position[1] + offsets[k][1] * s;
        v[k].position[2] = p->position[2];
        v[k].color = p->color;
    }
}

static void simulate_range(uint32_t first, uint32_t count, ParticleVertex *out, uint32_t *bounces)
{
    uint32_t local = 0;
    for (uint32_t i = first; i < first + count; ++i) {
        local += step(&sParticles[i]);
        emit(&sParticles[i], &out[i * 3]);
    }
    *bounces += local;
    // Contended shared state: every core updates both counters each frame.
    OSAddAtomic(&sAtomicBounces, (int32_t)local);
    OSLockMutex(&sMutex);
    sMutexBounces += local;
    OSUnlockMutex(&sMutex);
}

static int worker_main(int argc, const char **argv)
{
    Worker *worker = (Worker *)argv;
    (void)argc;
    for (;;) {
        OSMessage message;
        OSReceiveMessage(&worker->queue, &message, OS_MESSAGE_FLAGS_BLOCKING);
        if (message.args[0] == MSG_QUIT)
            break;
        simulate_range(worker->first, worker->count, sVertices[message.args[1]], &worker->bounces);
        DCFlushRange(&sVertices[message.args[1]][worker->first * 3], worker->count * 3 * sizeof(ParticleVertex));
        OSSendMessage(&sDone, &message, OS_MESSAGE_FLAGS_BLOCKING);
    }
    return 0;
}

bool particles_init(uint32_t count)
{
    sCount = count - count % WORKERS;
    sSteps = sBuffer = 0;
    sMutexBounces = 0;
    sAtomicBounces = 0;
    sParticles = gfx_alloc(sCount * sizeof(Particle), 64);
    sInitial = gfx_alloc(sCount * sizeof(Particle), 64);
    for (int b = 0; b < 2; ++b)
        sVertices[b] = gfx_alloc(sCount * 3 * sizeof(ParticleVertex), GX2_VERTEX_BUFFER_ALIGNMENT);
    if (!sParticles || !sInitial || !sVertices[0] || !sVertices[1])
        return report_error("particle allocation (%u)", (unsigned)sCount);
    for (uint32_t i = 0; i < sCount; ++i) {
        sParticles[i].seed = 0x9E3779B9u ^ (i * 2654435761u);
        respawn(&sParticles[i]);
        sParticles[i].age = i % sParticles[i].life;
    }
    memcpy(sInitial, sParticles, sCount * sizeof(Particle));
    if (!program_load(&sProgram, SHADER_PARTICLE, PARTICLE_ATTRIBS, 2, sizeof(ParticleVertex)))
        return false;

    OSInitMutex(&sMutex);
    OSInitMessageQueue(&sDone, sDoneMessages, 8);
    uint32_t slice = sCount / WORKERS;
    for (int i = 0; i < WORKERS; ++i) {
        Worker *worker = &sWorkers[i];
        memset(worker, 0, sizeof(*worker));
        worker->first = slice * (uint32_t)i;
        worker->count = slice;
        worker->stack = gfx_alloc(STACK_SIZE, 16);
        if (!worker->stack)
            return report_error("worker stack");
        OSInitMessageQueue(&worker->queue, worker->messages, 4);
        if (!OSCreateThread(&worker->thread, worker_main, 0, (char *)worker, worker->stack + STACK_SIZE, STACK_SIZE,
                            16, (OSThreadAttributes)(1 << i)))
            return report_error("OSCreateThread core %d", i);
        OSSetThreadName(&worker->thread, "wcs-particles");
        OSResumeThread(&worker->thread);
    }
    return true;
}

void particles_update(void)
{
    sBuffer ^= 1;
    for (int i = 0; i < WORKERS; ++i) {
        OSMessage message = {.message = NULL, .args = {MSG_STEP, sBuffer, 0}};
        OSSendMessage(&sWorkers[i].queue, &message, OS_MESSAGE_FLAGS_BLOCKING);
    }
    for (int i = 0; i < WORKERS; ++i) {
        OSMessage message;
        OSReceiveMessage(&sDone, &message, OS_MESSAGE_FLAGS_BLOCKING);
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, sVertices[sBuffer], sCount * 3 * sizeof(ParticleVertex));
    ++sSteps;
}

void particles_draw(const Mat4 *viewProj)
{
    program_bind(&sProgram);
    uniform_vs(0, viewProj->m, sizeof(viewProj->m));
    GX2SetDepthOnlyControl(TRUE, FALSE, GX2_COMPARE_FUNC_LESS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD,
                       TRUE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetAttribBuffer(0, sCount * 3 * sizeof(ParticleVertex), sizeof(ParticleVertex), sVertices[sBuffer]);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, sCount * 3, 0, 1);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
}

void particles_shutdown(void)
{
    for (int i = 0; i < WORKERS; ++i) {
        OSMessage message = {.message = NULL, .args = {MSG_QUIT, 0, 0}};
        OSSendMessage(&sWorkers[i].queue, &message, OS_MESSAGE_FLAGS_BLOCKING);
        int result;
        OSJoinThread(&sWorkers[i].thread, &result);
        gfx_free(sWorkers[i].stack);
    }
    program_free(&sProgram);
    for (int b = 0; b < 2; ++b)
        gfx_free(sVertices[b]);
    gfx_free(sParticles);
    gfx_free(sInitial);
}

bool particles_verify(void)
{
    uint32_t workerBounces = 0;
    for (int i = 0; i < WORKERS; ++i)
        workerBounces += sWorkers[i].bounces;

    // Replay on this thread from the saved initial state.
    Particle *threaded = sParticles;
    sParticles = sInitial;
    uint32_t reference = 0;
    for (uint32_t s = 0; s < sSteps; ++s)
        for (uint32_t i = 0; i < sCount; ++i)
            reference += step(&sParticles[i]);
    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < sCount; ++i)
        mismatches += memcmp(sParticles[i].position, threaded[i].position, sizeof(threaded[i].position)) != 0;
    sParticles = threaded;

    bool ok = mismatches == 0 && reference == workerBounces && reference == sMutexBounces &&
              (int32_t)reference == sAtomicBounces;
    report("detail particles=%u steps=%u mismatched=%u bounces=%u workers=%u mutex=%u atomic=%d", (unsigned)sCount,
           (unsigned)sSteps, (unsigned)mismatches, (unsigned)reference, (unsigned)workerBounces,
           (unsigned)sMutexBounces, (int)sAtomicBounces);
    return ok;
}
