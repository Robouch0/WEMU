#include "LaunchEnvironment.hpp"
#include <QDebug>

int main()
{
    QProcessEnvironment inherited;
    inherited.insert("DISPLAY", ":17");
    inherited.insert("PATH", "/usr/bin");
    inherited.insert("WEMU_NATIVE_CPU", "1");
    inherited.insert("WEMU_NATIVE_RASTER", "0");
    inherited.insert("WEMU_NO_FRAMESKIP", "0");
    inherited.insert("WEMU_NATIVE_VERIFY_EVERY", "1");
    inherited.insert("WEMU_SCENE_EXPERIMENT", "1");
    inherited.insert("WEMU_SHARED_FONT", "/wrong/font");
    const auto environment = desktopLaunchEnvironment(inherited, "/games/base content", "/fonts/shared font");
    for (const auto *key : {"WEMU_FRAME_CLOCK", "WEMU_NO_FRAMESKIP", "WEMU_NATIVE_RASTER", "WEMU_NATIVE_DEFER_READBACK",
                            "WEMU_NATIVE_LAZY_READBACK", "WEMU_NATIVE_RESIDENT_TEXTURES", "WEMU_NATIVE_RESIDENT_TARGETS"}) {
        if (environment.value(key) != "1") { qCritical() << "Missing desktop optimization" << key; return 1; }
    }
    for (const auto *key : {"WEMU_NATIVE_CPU", "WEMU_NATIVE_VERIFY_EVERY", "WEMU_SCENE_EXPERIMENT"}) {
        if (environment.contains(key)) { qCritical() << "Inherited experiment" << key; return 1; }
    }
    if (environment.value("DISPLAY") != ":17" || environment.value("PATH") != "/usr/bin"
        || environment.value("WEMU_CONTENT_LAYER") != "/games/base content"
        || environment.value("WEMU_SHARED_FONT") != "/fonts/shared font"
        || desktopLaunchEnvironment(inherited, "/base", {}).contains("WEMU_SHARED_FONT")) return 1;
    qInfo() << "Desktop GPU profile, host environment and experiment isolation passed";
    return 0;
}
