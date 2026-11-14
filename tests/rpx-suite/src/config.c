#include "config.h"

#include <coreinit/filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "report.h"

void config_load(Config *config)
{
    memset(config, 0, sizeof(*config));
    FSInit();
    FSClient *client = gfx_alloc(sizeof(FSClient), 64);
    FSCmdBlock *block = gfx_alloc(sizeof(FSCmdBlock), 64);
    uint8_t *buffer = gfx_alloc(sizeof(config->text), 64);
    if (!client || !block || !buffer)
        goto done;
    FSAddClient(client, FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(block);
    FSFileHandle handle;
    if (FSOpenFile(client, block, "/vol/content/suite.cfg", "r", &handle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        FSStatus read = FSReadFile(client, block, buffer, 1, sizeof(config->text) - 1, handle, 0, FS_ERROR_FLAG_ALL);
        if (read > 0)
            memcpy(config->text, buffer, (size_t)read);
        FSCloseFile(client, block, handle, FS_ERROR_FLAG_ALL);
        report("config bytes=%d", (int)read);
    } else {
        report("config default");
    }
    FSDelClient(client, FS_ERROR_FLAG_ALL);
done:
    gfx_free(buffer);
    gfx_free(block);
    gfx_free(client);
}

// Returns the value of `key=` on its own line, or NULL.
static const char *lookup(const Config *config, const char *key, char *value, size_t size)
{
    size_t length = strlen(key);
    for (const char *line = config->text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        if (strncmp(line, key, length) == 0 && line[length] == '=') {
            const char *start = line + length + 1;
            size_t n = strcspn(start, "\r\n");
            if (n >= size)
                n = size - 1;
            memcpy(value, start, n);
            value[n] = 0;
            return value;
        }
    }
    return NULL;
}

bool config_bench(const Config *config)
{
    char mode[32];
    return lookup(config, "mode", mode, sizeof(mode)) && strcmp(mode, "bench") == 0;
}

bool config_scene_enabled(const Config *config, const char *scene)
{
    char list[512];
    if (!lookup(config, "scenes", list, sizeof(list)))
        return true;
    size_t length = strlen(scene);
    for (const char *item = list; *item;) {
        size_t n = strcspn(item, ",");
        if (n == length && strncmp(item, scene, n) == 0)
            return true;
        item += n;
        if (*item == ',')
            ++item;
    }
    return false;
}

uint32_t config_frames(const Config *config, const char *scene, uint32_t fallback)
{
    char key[64], value[32];
    snprintf(key, sizeof(key), "%s.frames", scene);
    if (lookup(config, key, value, sizeof(value)) || lookup(config, "frames", value, sizeof(value)))
        return (uint32_t)strtoul(value, NULL, 10);
    return fallback;
}
