#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QSettings>
#include <QDir>
#include <SDL2/SDL.h>
#include "input/InputManager.hpp"
#include "input/InputProfileManager.hpp"
#include "input/KeyboardInput.hpp"
#include "library/TitleScanner.hpp"
#include "emulator/EmulatorLauncher.hpp"

int main(int argc, char *argv[])
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) qFatal("SDL_Init failed: %s", SDL_GetError());
    QGuiApplication app(argc, argv);
    app.setOrganizationName("WEMU");
    app.setApplicationName("WEMU");
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
    if (!library.isEmpty()) scanner.scanDirectory(library);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("InputManager", &input);
    engine.rootContext()->setContextProperty("InputProfileManager", &profiles);
    engine.rootContext()->setContextProperty("TitleScanner", &scanner);
    engine.rootContext()->setContextProperty("EmulatorLauncher", &launcher);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &input, &InputManager::stopPolling);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &launcher, &EmulatorLauncher::stop);
    engine.load(QUrl("qrc:/assets/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    const int result = app.exec();
    input.stopPolling();
    return result;
}
