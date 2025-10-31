#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "input/InputManager.hpp"
#include <QDebug>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    auto inputManager = new InputManager(&app);
    engine.rootContext()->setContextProperty("InputManager", inputManager);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl("qrc:/assets/qml/Main.qml"));

    qDebug() << "Emulator GUI started with persistent InputManager";

    return app.exec();
}
