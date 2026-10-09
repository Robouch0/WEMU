#pragma once

#include <QProcessEnvironment>
#include <QString>

inline QProcessEnvironment desktopLaunchEnvironment(QProcessEnvironment environment, const QString &contentPath,
                                                    const QString &sharedFont)
{
    // Keep host display/audio configuration, but never inherit emulation experiments.
    for (const auto &key : environment.keys())
        if (key.startsWith("WEMU_")) environment.remove(key);
    environment.insert("WEMU_FRAME_CLOCK", "1");
    environment.insert("WEMU_CONTENT_LAYER", contentPath);
    for (const auto *key : {"WEMU_NATIVE_RASTER", "WEMU_NATIVE_DEFER_READBACK", "WEMU_NATIVE_LAZY_READBACK",
                            "WEMU_NATIVE_RESIDENT_TEXTURES", "WEMU_NATIVE_RESIDENT_TARGETS"})
        environment.insert(key, "1");
    if (!sharedFont.isEmpty()) environment.insert("WEMU_SHARED_FONT", sharedFont);
    return environment;
}
