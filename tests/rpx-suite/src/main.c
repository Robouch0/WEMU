// WEMU RPX test suite: a homebrew title that exercises GX2 and the OS the way
// retail games do. With mode=bench in /vol/content/suite.cfg (written by
// tests/rpx-suite/run.py) it runs the timed scenes and logs "[WCS]" lines;
// otherwise (GUI launch) it starts an interactive viewer with an overlay.

#include <coreinit/foreground.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <proc_ui/procui.h>
#include <stdio.h>
#include <string.h>
#include <sysapp/launch.h>
#include <vpad/input.h>

#include "arena.h"
#include "config.h"
#include "gfx.h"
#include "input.h"
#include "overlay.h"
#include "report.h"
#include "scenes.h"
#include "world.h"

#define SUITE_VERSION 3
#define CHECK_WARMUP_FRAMES 300 // interactive checks restart the scene and run this long first

static bool running(void)
{
    return ProcUIProcessMessages(TRUE) != PROCUI_STATUS_EXITING;
}

typedef struct
{
    bool ok;
    uint32_t probes, frames;
    unsigned errors;
    char detail[96]; // the workload the scene reported, for the summary screen
} SceneResult;

typedef void (*FrameHud)(const Scene *scene, uint32_t frame, uint32_t frames, void *context);

// One timed scene followed by its checks, logged with the markers run.py parses.
// `hud` (optional) draws an overlay on each timed frame; + aborts when it is set.
static SceneResult run_scene(const Scene *scene, uint32_t frames, Targets *targets, FrameHud hud, void *context)
{
    SceneResult result = {0};
    unsigned errors = report_error_count();
    bool ok = scene->init();
    report("scene-begin name=%s frames=%u present=%u", scene->name, (unsigned)frames, (unsigned)gfx_frame_index());
    uint32_t frame = 0;
    OSTime start = OSGetTime();
    for (; ok && frame < frames && running(); ++frame) {
        gfx_begin_frame();
        bool drcDrawn = scene->render(targets, frame);
        if (hud) {
            hud(scene, frame, frames, context);
            overlay_draw(targets->tv);
            if (drcDrawn)
                overlay_draw(targets->drc);
            if (input_pressed() & VPAD_BUTTON_PLUS)
                frames = frame + 1;
        }
        gfx_present(targets->tv, drcDrawn ? targets->drc : NULL);
    }
    // Guest time only: host throughput comes from the emulator's frame profile.
    report("scene-end name=%s frames=%u present=%u guest_ms=%u", scene->name, (unsigned)frame,
           (unsigned)gfx_frame_index(), (unsigned)OSTicksToMilliseconds(OSGetTime() - start));
    probes_shown();
    report_clear_detail();
    ok = ok && scene->check(targets) && report_error_count() == errors;
    result.probes = probes_shown();
    result.frames = frame;
    result.errors = report_error_count() - errors;
    snprintf(result.detail, sizeof(result.detail), "%.95s", report_last_detail());
    report("check name=%s result=%s errors=%u", scene->name, ok ? "PASS" : "FAIL", result.errors);
    scene->shutdown();
    result.ok = ok;
    return result;
}

static void run_bench(const Config *config, Targets *targets)
{
    unsigned passed = 0, failed = 0;
    for (uint32_t s = 0; s < SCENE_COUNT && running(); ++s) {
        const Scene *scene = &SCENES[s];
        if (!config_scene_enabled(config, scene->name))
            continue;
        SceneResult result = run_scene(scene, config_frames(config, scene->name, scene->frames), targets, NULL, NULL);
        result.ok ? ++passed : ++failed;
    }
    report("suite-end pass=%u fail=%u errors=%u", passed, failed, report_error_count());
}

// ---------------------------------------------------------------------------
// Interactive viewer

typedef struct
{
    uint32_t scene, frame;
    bool paused, hud, sceneOk;
    uint32_t checkCountdown; // frames until the check runs; 0 = idle
    char result[96];
    OSTime frameTimes[30];
    uint32_t frameCount;
} Viewer;

static uint32_t next_enabled(const Config *config, uint32_t from, int step)
{
    for (uint32_t i = 1; i <= SCENE_COUNT; ++i) {
        uint32_t index = (from + SCENE_COUNT + (uint32_t)step * i) % SCENE_COUNT;
        if (config_scene_enabled(config, SCENES[index].name))
            return index;
    }
    return from;
}

static void enter_scene(Viewer *viewer, uint32_t index)
{
    viewer->scene = index;
    viewer->frame = 0;
    viewer->checkCountdown = 0;
    viewer->sceneOk = SCENES[index].init();
    snprintf(viewer->result, sizeof(viewer->result), "%s", viewer->sceneOk ? "not run - press A" : "init failed");
}

static float guest_fps(Viewer *viewer)
{
    OSTime now = OSGetTime();
    uint32_t slot = viewer->frameCount++ % 30;
    OSTime oldest = viewer->frameTimes[slot];
    viewer->frameTimes[slot] = now;
    if (viewer->frameCount <= 30 || now <= oldest)
        return 0.0f;
    return 30.0f * 1000.0f / (float)OSTicksToMilliseconds(now - oldest);
}

static void viewer_hud(Viewer *viewer, float fps)
{
    const Scene *scene = &SCENES[viewer->scene];
    overlay_begin();
    overlay_rect(0, 0, TV_WIDTH, 92, 0x000000B0);
    overlay_text(20, 10, 1.5f, 0xFFFFFFFF, "WEMU RPX TEST SUITE   %u/%u  %s", (unsigned)viewer->scene + 1,
                 (unsigned)SCENE_COUNT, scene->name);
    overlay_text(20, 40, 1.0f, 0xC0C0C0FF, "%s", scene->summary);
    overlay_text(20, 62, 1.0f, 0xC0C0C0FF, "frame %u%s   guest %.1f fps", (unsigned)viewer->frame,
                 viewer->paused ? " (paused)" : "", (double)fps);
    uint32_t color = viewer->result[0] == 'P' ? 0x60FF60FF : (viewer->result[0] == 'F' ? 0xFF6060FF : 0xFFE000FF);
    if (viewer->checkCountdown)
        overlay_text(700, 62, 1.0f, 0xFFE000FF, "checks: warming up %us", (unsigned)viewer->checkCountdown / 60 + 1);
    else
        overlay_text(700, 62, 1.0f, color, "checks: %s", viewer->result);
    overlay_rect(0, TV_HEIGHT - 30, TV_WIDTH, 30, 0x000000B0);
    overlay_text(20, TV_HEIGHT - 24, 1.0f, 0xFFFFFFFF,
                 "L/R: scene   A: run checks   -: full sequence + summary   B: pause   X: hide overlay   +: quit");
}

static void run_check(Viewer *viewer, Targets *targets)
{
    const Scene *scene = &SCENES[viewer->scene];
    unsigned errors = report_error_count();
    probes_shown();
    bool ok = scene->check(targets) && report_error_count() == errors;
    uint32_t probes = probes_shown();
    if (probes)
        snprintf(viewer->result, sizeof(viewer->result), "%s self-checks, %u probes shown (compare by eye)",
                 ok ? "PASS" : "FAIL", (unsigned)probes);
    else
        snprintf(viewer->result, sizeof(viewer->result), "%s self-checks", ok ? "PASS" : "FAIL");
}

// ---------------------------------------------------------------------------
// Full sequence (- button): every scene timed then checked, ending on a summary.

#define SEQUENCE_FRAMES 600 // per scene; endurance keeps its longer default
#define SEQUENCE_PROBE_HOLD 90

typedef struct
{
    uint32_t index, count;
} SequenceHud;

static void sequence_hud(const Scene *scene, uint32_t frame, uint32_t frames, void *context)
{
    const SequenceHud *hud = context;
    overlay_begin();
    overlay_rect(0, 0, TV_WIDTH, 64, 0x000000B0);
    overlay_text(20, 10, 1.5f, 0xFFFFFFFF, "SEQUENCE %u/%u  %s", (unsigned)hud->index, (unsigned)hud->count, scene->name);
    overlay_text(20, 40, 1.0f, 0xC0C0C0FF, "frame %u/%u   %s   (+: skip scene)", (unsigned)frame, (unsigned)frames,
                 scene->summary);
    overlay_rect(0, 60, TV_WIDTH * (float)frame / (float)frames, 4, 0x3987E5FF);
}

static void draw_summary(const Config *config, const SceneResult *results, uint32_t completed)
{
    unsigned passed = 0, probes = 0;
    overlay_begin();
    overlay_rect(0, 0, TV_WIDTH, TV_HEIGHT, 0x101012F0);
    overlay_text(40, 24, 2.0f, 0xFFFFFFFF, "SEQUENCE SUMMARY");
    overlay_text(40, 62, 1.0f, 0xC3C2B7FF, "What each scene checked itself: errors, draw counts, multi-core results.");
    float y = 96;
    overlay_text(40, y, 1.0f, 0x8F8E86FF, "scene                self-check    frames  probes  workload");
    y += 24;
    for (uint32_t s = 0, row = 0; s < SCENE_COUNT; ++s) {
        if (!config_scene_enabled(config, SCENES[s].name))
            continue;
        const SceneResult *r = &results[s];
        bool ran = row < completed;
        overlay_text(40, y, 1.0f, 0xF0EFECFF, "%-20s", SCENES[s].name);
        if (ran) {
            // Status pill: colour plus a label, never colour alone.
            overlay_rect(208, y - 2, 96, 20, r->ok ? 0x0CA30CFF : 0xD03B3BFF);
            overlay_text(216, y, 1.0f, 0xFFFFFFFF, r->ok ? "+ PASS" : "x FAIL");
            overlay_text(336, y, 1.0f, 0xF0EFECFF, "%5u", (unsigned)r->frames);
            overlay_text(408, y, 1.0f, 0xF0EFECFF, "%4u", (unsigned)r->probes);
            if (r->errors)
                overlay_text(472, y, 1.0f, 0xFAB219FF, "%u error(s), see the session log", r->errors);
            else
                overlay_text(472, y, 1.0f, 0xC3C2B7FF, "%.96s", r->detail[0] ? r->detail : "-");
            passed += r->ok;
            probes += r->probes;
        } else {
            overlay_text(216, y, 1.0f, 0x8F8E86FF, "skipped");
        }
        y += 26;
        ++row;
    }
    y += 10;
    // Status as a pill with a label, like the rows: never colour alone.
    overlay_rect(40, y - 2, 96, 28, passed == completed ? 0x0CA30CFF : 0xD03B3BFF);
    overlay_text(48, y, 1.5f, 0xFFFFFFFF, passed == completed ? "+ PASS" : "x FAIL");
    overlay_text(152, y, 1.5f, 0xF0EFECFF, "%u / %u scenes passed their self-checks, %u probes shown", passed,
                 (unsigned)completed, probes);
    overlay_text(40, y + 44, 1.0f, 0xC3C2B7FF,
                 "Not on this screen: FPS, stutters, memory, and whether each probe pixel is right.\n"
                 "The console cannot time its host (its clock advances once per frame) or read back a frame yet.");
    overlay_text(40, y + 92, 1.0f, 0xFFFFFFFF, "Full regression report, on the PC:   tests/rpx-suite/run.py --analyze-last");
    overlay_text(40, y + 114, 1.0f, 0xC3C2B7FF, "(start the GUI with WEMU_FRAME_PROFILE=1 to include FPS, stutters and pacing)");
    overlay_text(40, TV_HEIGHT - 40, 1.0f, 0xFFFFFFFF, "A / B: back to the viewer");
}

static void run_sequence(const Config *config, Targets *targets)
{
    static SceneResult results[32];
    memset(results, 0, sizeof(results));
    SequenceHud hud = {0, 0};
    for (uint32_t s = 0; s < SCENE_COUNT; ++s)
        hud.count += config_scene_enabled(config, SCENES[s].name);
    probes_set_hold(SEQUENCE_PROBE_HOLD);
    report("suite-begin version=%d scenes=%u mode=sequence", SUITE_VERSION, (unsigned)hud.count);
    unsigned passed = 0, failed = 0;
    uint32_t completed = 0;
    for (uint32_t s = 0; s < SCENE_COUNT && running(); ++s) {
        const Scene *scene = &SCENES[s];
        if (!config_scene_enabled(config, scene->name))
            continue;
        hud.index = completed + 1;
        uint32_t frames = scene->frames > SEQUENCE_FRAMES && strcmp(scene->name, "endurance") ? SEQUENCE_FRAMES : scene->frames;
        results[s] = run_scene(scene, config_frames(config, scene->name, frames), targets, sequence_hud, &hud);
        results[s].ok ? ++passed : ++failed;
        ++completed;
    }
    report("suite-end pass=%u fail=%u errors=%u", passed, failed, report_error_count());
    probes_set_hold(0);
    while (running()) {
        uint32_t pressed = input_pressed();
        if (pressed & (VPAD_BUTTON_A | VPAD_BUTTON_B | VPAD_BUTTON_PLUS))
            break;
        gfx_begin_frame();
        GX2ClearColor(&targets->tv->color, 0, 0, 0, 1);
        draw_summary(config, results, completed);
        overlay_draw(targets->tv);
        gfx_present(targets->tv, NULL);
    }
}

static void run_interactive(const Config *config, Targets *targets)
{
    static Viewer viewer = {.hud = true};
    probes_set_interactive(true);
    enter_scene(&viewer, next_enabled(config, SCENE_COUNT - 1, 1));
    while (running()) {
        uint32_t pressed = input_pressed();
        if (pressed & VPAD_BUTTON_PLUS)
            break;
        if (pressed & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_R | VPAD_BUTTON_LEFT | VPAD_BUTTON_L)) {
            int step = (pressed & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_R)) ? 1 : -1;
            SCENES[viewer.scene].shutdown();
            enter_scene(&viewer, next_enabled(config, viewer.scene, step));
        }
        if (pressed & VPAD_BUTTON_B)
            viewer.paused = !viewer.paused;
        if (pressed & VPAD_BUTTON_X)
            viewer.hud = !viewer.hud;
        if (pressed & VPAD_BUTTON_MINUS) {
            SCENES[viewer.scene].shutdown();
            run_sequence(config, targets);
            enter_scene(&viewer, viewer.scene);
            continue;
        }
        if ((pressed & VPAD_BUTTON_A) && !viewer.checkCountdown) {
            // Same conditions as the bench: a fresh scene, then its checks.
            SCENES[viewer.scene].shutdown();
            enter_scene(&viewer, viewer.scene);
            viewer.paused = false;
            viewer.checkCountdown = CHECK_WARMUP_FRAMES;
        }

        gfx_begin_frame();
        bool drcDrawn = viewer.sceneOk && SCENES[viewer.scene].render(targets, viewer.frame);
        float fps = guest_fps(&viewer);
        if (viewer.hud) {
            viewer_hud(&viewer, fps);
            overlay_draw(targets->tv);
            if (drcDrawn)
                overlay_draw(targets->drc);
        }
        gfx_present(targets->tv, drcDrawn ? targets->drc : NULL);
        if (!viewer.paused)
            ++viewer.frame;
        if (viewer.checkCountdown && --viewer.checkCountdown == 0)
            run_check(&viewer, targets);
    }
    SCENES[viewer.scene].shutdown();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ProcUIInit(OSSavesDone_ReadyToRelease);

    static Config config;
    config_load(&config);
    bool bench = config_bench(&config);
    report("suite-begin version=%d scenes=%u mode=%s", SUITE_VERSION, (unsigned)SCENE_COUNT,
           bench ? "bench" : "interactive");

    RenderTarget tv, drc;
    if (!gfx_init() || !target_init(&tv, TV_WIDTH, TV_HEIGHT, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, true, false) ||
        !target_init(&drc, DRC_WIDTH, DRC_HEIGHT, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, true, false) ||
        !world_init() || !arena_init() || !probes_init() || !overlay_init()) {
        report("suite-end pass=0 fail=0 errors=%u aborted=init", report_error_count());
        ProcUIShutdown();
        return 1;
    }
    Targets targets = {&tv, &drc};
    if (bench)
        run_bench(&config, &targets);
    else
        run_interactive(&config, &targets);

    overlay_shutdown();
    probes_shutdown();
    arena_shutdown();
    world_shutdown();
    gfx_shutdown();
    // Retail exit path: ask for the menu, then pump ProcUI until it says EXITING.
    // Bounded, because an emulator may never deliver the exit message.
    SYSLaunchMenu();
    for (int i = 0; i < 600 && running(); ++i)
        OSSleepTicks(OSMillisecondsToTicks(16));
    ProcUIShutdown();
    return 0;
}
