#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include "IInputDevice.hpp"
#include "InputManager.hpp"
#include "KeyboardInput.hpp"

int main(int argc, char *argv[])
{
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

    return app.exec();
}
