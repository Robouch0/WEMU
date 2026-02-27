#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <SDL2/SDL.h>
#include "input/IInputDevice.hpp"
#include "input/InputManager.hpp"
#include "input/KeyboardInput.hpp"

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qFatal("SDL_Init failed: %s", SDL_GetError());
    }
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    auto inputManager = new InputManager();
    auto keyboard = new KeyboardInput();
    inputManager->addDevice(keyboard);

    engine.rootContext()->setContextProperty("InputManager", inputManager);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl("qrc:/assets/qml/Main.qml"));

    qDebug() << "Emulator GUI started with persistent InputManager";

    const int ret = app.exec();
    SDL_Quit();
    return ret;
}
