#pragma once

#include <QProcessEnvironment>
#include <QString>

inline QProcessEnvironment desktopLaunchEnvironment(QProcessEnvironment environment, const QString &contentPath,
                                                    const QString &sharedFont, bool nativeCpu = false,
                                                    const QString &nativeCache = {}, unsigned language = 1,
                                                    unsigned region = 4, unsigned titleRegion = 4, bool x64Cpu = false)
{
    // Keep host display/audio configuration, but never inherit emulation experiments.
    for (const auto &key : environment.keys())
        if (key.startsWith("WEMU_")) environment.remove(key);
    environment.insert("WEMU_FRAME_CLOCK", "1");
    // Captured-command hashes do not cover live texture/vertex bytes. Replay
    // every frame and let the GPU caches reuse verified resources instead.
    environment.insert("WEMU_NO_FRAMESKIP", "1");
    environment.insert("WEMU_CONTENT_LAYER", contentPath);
    environment.insert("WEMU_SYSTEM_LANGUAGE", QString::number(language));
    environment.insert("WEMU_SYSTEM_REGION", QString::number(region));
    environment.insert("WEMU_TITLE_REGION", QString::number(titleRegion));
    for (const auto *key : {"WEMU_NATIVE_RASTER", "WEMU_NATIVE_DEFER_READBACK", "WEMU_NATIVE_LAZY_READBACK",
                            "WEMU_NATIVE_RESIDENT_TEXTURES", "WEMU_NATIVE_RESIDENT_TARGETS"})
        environment.insert(key, "1");
    if (!sharedFont.isEmpty()) environment.insert("WEMU_SHARED_FONT", sharedFont);
    if (nativeCpu) {
        if (x64Cpu) {
            environment.insert("WEMU_X64_JIT", "1");
        } else {
            environment.insert("WEMU_NATIVE_CPU", "1");
            if (!nativeCache.isEmpty()) environment.insert("WEMU_NATIVE_CACHE_DIR", nativeCache);
        }
    }
    if (environment.value("QT_WEMU_INPUT_TRACE") == "1") environment.insert("WEMU_VPAD_TRACE", "1");
    return environment;
}
