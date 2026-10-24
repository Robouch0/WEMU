#include "LaunchEnvironment.hpp"
#include <QDebug>

int main()
{
    QProcessEnvironment inherited;
    inherited.insert("DISPLAY", ":17");
    inherited.insert("PATH", "/usr/bin");
    inherited.insert("WEMU_NATIVE_CPU", "1");
    inherited.insert("WEMU_X64_JIT", "1");
    inherited.insert("WEMU_NATIVE_CACHE_DIR", "/inherited/cache");
    inherited.insert("WEMU_SYSTEM_LANGUAGE", "ja");
    inherited.insert("WEMU_SYSTEM_REGION", "1");
    inherited.insert("WEMU_TITLE_REGION", "1");
    inherited.insert("WEMU_NATIVE_RASTER", "0");
    inherited.insert("WEMU_NO_FRAMESKIP", "0");
    inherited.insert("WEMU_NATIVE_VERIFY_EVERY", "1");
    inherited.insert("WEMU_SCENE_EXPERIMENT", "1");
    inherited.insert("WEMU_SHARED_FONT", "/wrong/font");
    inherited.insert("WEMU_VPAD_TRACE", "1");
    const auto environment = desktopLaunchEnvironment(inherited, "/games/base content", "/fonts/shared font");
    for (const auto *key : {"WEMU_FRAME_CLOCK", "WEMU_NO_FRAMESKIP", "WEMU_NATIVE_RASTER", "WEMU_NATIVE_DEFER_READBACK",
                            "WEMU_NATIVE_LAZY_READBACK", "WEMU_NATIVE_RESIDENT_TEXTURES", "WEMU_NATIVE_RESIDENT_TARGETS"}) {
        if (environment.value(key) != "1") { qCritical() << "Missing desktop optimization" << key; return 1; }
    }
    for (const auto *key : {"WEMU_NATIVE_CPU", "WEMU_X64_JIT", "WEMU_NATIVE_CACHE_DIR", "WEMU_NATIVE_VERIFY_EVERY", "WEMU_SCENE_EXPERIMENT", "WEMU_VPAD_TRACE"}) {
        if (environment.contains(key)) { qCritical() << "Inherited experiment" << key; return 1; }
    }
    if (environment.value("DISPLAY") != ":17" || environment.value("PATH") != "/usr/bin"
        || environment.value("WEMU_CONTENT_LAYER") != "/games/base content"
        || environment.value("WEMU_SHARED_FONT") != "/fonts/shared font"
        || desktopLaunchEnvironment(inherited, "/base", {}).contains("WEMU_SHARED_FONT")) return 1;
    inherited.insert("QT_WEMU_INPUT_TRACE", "1");
    if (desktopLaunchEnvironment(inherited, "/base", {}).value("WEMU_VPAD_TRACE") != "1") return 1;
    const auto native = desktopLaunchEnvironment(inherited, "/base", {}, true, "/private/cache with spaces");
    if (native.value("WEMU_NATIVE_CPU") != "1" || native.value("WEMU_NATIVE_CACHE_DIR") != "/private/cache with spaces"
        || native.contains("WEMU_NATIVE_VERIFY_EVERY")) return 1;
    const auto noCache = desktopLaunchEnvironment(inherited, "/base", {}, true);
    if (noCache.value("WEMU_NATIVE_CPU") != "1" || noCache.contains("WEMU_NATIVE_CACHE_DIR")) return 1;
    const auto reference = desktopLaunchEnvironment(inherited, "/base", {}, false, "/ignored");
    if (reference.contains("WEMU_NATIVE_CPU") || reference.contains("WEMU_NATIVE_CACHE_DIR")) return 1;
    const auto x64 = desktopLaunchEnvironment(inherited, "/base", {}, true, "/ignored", 1, 4, 4, true);
    if (x64.value("WEMU_X64_JIT") != "1" || x64.contains("WEMU_NATIVE_CPU") || x64.contains("WEMU_NATIVE_CACHE_DIR")) return 1;
    const auto disabled = desktopLaunchEnvironment(inherited, "/base", {}, false, "/ignored", 1, 4, 4, true);
    if (disabled.contains("WEMU_X64_JIT") || disabled.contains("WEMU_NATIVE_CPU") || disabled.contains("WEMU_NATIVE_CACHE_DIR")) return 1;
    if (environment.value("WEMU_SYSTEM_LANGUAGE") != "1" || environment.value("WEMU_SYSTEM_REGION") != "4"
        || environment.value("WEMU_TITLE_REGION") != "4") return 1;
    const auto french = desktopLaunchEnvironment(inherited, "/base", {}, true, "/private", 2, 2, 4);
    if (french.value("WEMU_SYSTEM_LANGUAGE") != "2" || french.value("WEMU_SYSTEM_REGION") != "2"
        || french.value("WEMU_TITLE_REGION") != "4") return 1;
    qInfo() << "Desktop GPU profile, host environment and experiment isolation passed";
    return 0;
}
