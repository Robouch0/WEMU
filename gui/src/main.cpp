#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <SDL2/SDL.h>

#include "emulator/EmulatorLauncher.hpp"
#include "emulator/GameView.hpp"
#include "emulator/SessionProbe.hpp"
#include "input/InputManager.hpp"
#include "input/InputProfileManager.hpp"
#include "input/KeyboardInput.hpp"
#include "library/TitleScanner.hpp"
#include "remote/RemoteGamepadServer.hpp"

int main(int argc, char *argv[])
{
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
    QQmlApplicationEngine engine;
    qmlRegisterType<GameView>("Wemu", 1, 0, "GameView");
    engine.rootContext()->setContextProperty("InputManager", &input);
    engine.rootContext()->setContextProperty("InputProfileManager", &profiles);
    engine.rootContext()->setContextProperty("TitleScanner", &scanner);
    engine.rootContext()->setContextProperty("EmulatorLauncher", &launcher);
    engine.rootContext()->setContextProperty("RemoteGamepad", &remoteGamepad);
    QObject::connect(&input, &InputManager::inputUpdated, &launcher, [&input, &profiles, &launcher] {
        const auto pad = input.controllerState(profiles.bindings());
        launcher.setControllerInput(pad.buttons, pad.leftStick, pad.rightStick);
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &input, &InputManager::stopPolling);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &launcher, &EmulatorLauncher::stop);
    engine.load(QUrl("qrc:/assets/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
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
