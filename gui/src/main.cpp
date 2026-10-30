#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <SDL2/SDL.h>

#include "emulator/EmulatorLauncher.hpp"
#include "emulator/GameView.hpp"
#include "emulator/SessionProbe.hpp"
#include "gfx/GlobeDots.hpp"
#include "i18n/AppLanguage.hpp"
#include "input/InputManager.hpp"
#include "input/InputProfileManager.hpp"
#include "input/KeyboardInput.hpp"
#include "input/MenuKeyBridge.hpp"
#include "library/TitleScanner.hpp"
#include "remote/RemoteGamepadServer.hpp"
#include "settings/AppPreferences.hpp"

namespace {
    void configureGpuPreference()
    {
#ifdef Q_OS_LINUX
        // PRIME must be configured before SDL/Qt initialize graphics. Respect explicit device choices.
        for (const auto *key : {"__NV_PRIME_RENDER_OFFLOAD", "__NV_PRIME_RENDER_OFFLOAD_PROVIDER",
                                "__GLX_VENDOR_LIBRARY_NAME", "__VK_LAYER_NV_optimus", "DRI_PRIME",
                                "VK_DRIVER_FILES", "VK_ICD_FILENAMES"}) {
            if (qEnvironmentVariableIsSet(key)) return;
        }
        // A render node bound to nvidia identifies an available NVIDIA driver, including its open kernel module.
        const QDir drm(QStringLiteral("/sys/class/drm"));
        for (const auto &node : drm.entryList({QStringLiteral("renderD*")}, QDir::Dirs)) {
            const auto driver = QFileInfo(drm.filePath(node + "/device/driver")).canonicalFilePath();
            if (QFileInfo(driver).fileName() != "nvidia") continue;
            qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
            qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
            qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
            qInfo("[GPU] NVIDIA PRIME offloading enabled");
            break;
        }
#endif
    }
}

int main(int argc, char *argv[])
{
    configureGpuPreference();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) qFatal("SDL_Init failed: %s", SDL_GetError());
    QGuiApplication app(argc, argv);
    app.setOrganizationName("WEMU");
    app.setApplicationName("WEMU");
#ifdef Q_OS_MACOS
    // The native macOS style ignores the custom backgrounds the QML controls draw; use the Linux default.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) QQuickStyle::setStyle("Fusion");
#endif
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"library", "Select a game library folder.", "directory"});
    parser.process(app);
    AppLanguage language;
    AppPreferences preferences;
    InputManager input;
    auto *keyboard = new KeyboardInput();
    input.addDevice(keyboard);
    InputProfileManager profiles;
    TitleScanner scanner;
    EmulatorLauncher launcher;
    auto library = parser.isSet("library") ? parser.value("library") : QSettings().value("library/directory").toString();
    if (!parser.isSet("library") && (library.isEmpty() || !QDir(library).exists()))
        library = TitleScanner::defaultLibraryPath(QCoreApplication::applicationDirPath(), QDir::currentPath());
    if (!library.isEmpty()) scanner.scanDirectory(library, !parser.isSet("library"));
    RemoteGamepadServer remoteGamepad(launcher, scanner);
    MenuKeyBridge menuKeys([&launcher] { return !launcher.running(); });
    QObject::connect(&input, &InputManager::buttonChanged, &menuKeys, &MenuKeyBridge::onButtonChanged);
    QObject::connect(&input, &InputManager::axisChanged, &menuKeys, &MenuKeyBridge::onAxisChanged);
    QQmlApplicationEngine engine;
    qmlRegisterType<GameView>("Wemu", 1, 0, "GameView");
    qmlRegisterType<GlobeDots>("Wemu", 1, 0, "GlobeDots");
    engine.rootContext()->setContextProperty("InputManager", &input);
    engine.rootContext()->setContextProperty("InputProfileManager", &profiles);
    engine.rootContext()->setContextProperty("TitleScanner", &scanner);
    engine.rootContext()->setContextProperty("EmulatorLauncher", &launcher);
    engine.rootContext()->setContextProperty("RemoteGamepad", &remoteGamepad);
    engine.rootContext()->setContextProperty("MenuKeys", &menuKeys);
    engine.rootContext()->setContextProperty("AppLanguage", &language);
    engine.rootContext()->setContextProperty("AppPreferences", &preferences);
    QObject::connect(&input, &InputManager::inputUpdated, &launcher, [&input, &profiles, &launcher] {
        const auto pad = input.controllerState(profiles.bindings());
        launcher.setControllerInput(pad.buttons, pad.leftStick, pad.rightStick);
    });
    QObject::connect(&language, &AppLanguage::codeChanged, &engine, &QQmlEngine::retranslate);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &input, &InputManager::stopPolling);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &launcher, &EmulatorLauncher::stop);
    engine.load(QUrl("qrc:/assets/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    menuKeys.setWindow(qobject_cast<QWindow *>(engine.rootObjects().first()));
    auto *gameView = engine.rootObjects().first()->findChild<GameView *>("gameView");
    if (!gameView)
        qFatal("The game presentation item was not created");
    QObject::connect(&launcher, &EmulatorLauncher::frameReady, gameView, &GameView::setFrame);
    QObject::connect(gameView, &GameView::framePresented, &launcher, &EmulatorLauncher::acknowledgeFrame);
    installSessionProbe(app, launcher, *gameView);
    const int result = app.exec();
    input.stopPolling();
    return result;
}
