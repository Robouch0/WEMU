#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "../../core/src/utils/EmulatorState.hpp"
#include <QDir>
#include <QDebug>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QDir resDir(":/qml");  // resource prefix
    QStringList files = resDir.entryList(QDir::Files);
    for (const auto& f : files) {
        qDebug() << f;
    }
    EmulatorState emulator;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("emulator", &emulator);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
