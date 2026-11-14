#pragma once

#include <stdbool.h>
#include <stdint.h>

// Optional /vol/content/suite.cfg written by the runner:
//   mode=bench                     (timed suite; anything else starts the interactive mode)
//   scenes=forward_lit,shadows     (default: all, in table order)
//   frames=300                     (overrides every scene)
//   shadows.frames=120             (per scene)
typedef struct
{
    char text[2048];
} Config;

void config_load(Config *config);
bool config_bench(const Config *config);
bool config_scene_enabled(const Config *config, const char *scene);
uint32_t config_frames(const Config *config, const char *scene, uint32_t fallback);
