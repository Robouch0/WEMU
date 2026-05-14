#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <QDir>
#include <SDL2/SDL.h>
#include "input/IInputDevice.hpp"
#include "input/InputManager.hpp"
#include "input/InputProfileManager.hpp"
#include "input/KeyboardInput.hpp"
#include "library/TitleScanner.hpp"

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qFatal("SDL_Init failed: %s", SDL_GetError());
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("wemu");
    QQmlApplicationEngine engine;

    auto inputManager = new InputManager();
    auto keyboard = new KeyboardInput();
    auto titleScanner = new TitleScanner(&app);
    inputManager->addDevice(keyboard);

    auto inputProfileManager = new InputProfileManager();

    engine.rootContext()->setContextProperty("InputManager", inputManager);
    engine.rootContext()->setContextProperty("InputProfileManager", inputProfileManager);
    engine.rootContext()->setContextProperty("TitleScanner", titleScanner);

    const QString gamesPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../games");
    titleScanner->scanDirectory(gamesPath);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl("qrc:/assets/qml/Main.qml"));

    qDebug() << "Emulator GUI started with persistent InputManager";

    const int ret = app.exec();
    SDL_Quit();
    return ret;
}
