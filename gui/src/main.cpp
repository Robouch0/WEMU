#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <QDir>
#include <QQuickWindow>
#include <QTimer>
#include <SDL2/SDL.h>
#include "input/IInputDevice.hpp"
#include "input/InputManager.hpp"
#include "input/InputProfileManager.hpp"
#include "input/KeyboardInput.hpp"
#include "library/TitleScanner.hpp"
#include "emulator/EmulatorLauncher.hpp"
#include "gfx/VulkanOutputWindow.hpp"

int main(int argc, char *argv[])
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qFatal("SDL_Init failed: %s", SDL_GetError());
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("wemu");

    app.setQuitOnLastWindowClosed(false);
    QQmlApplicationEngine engine;

    auto inputManager = new InputManager();
    auto keyboard = new KeyboardInput();
    auto titleScanner = new TitleScanner(&app);
    inputManager->addDevice(keyboard);

    auto inputProfileManager = new InputProfileManager();

    engine.rootContext()->setContextProperty("InputManager", inputManager);
    engine.rootContext()->setContextProperty("InputProfileManager", inputProfileManager);
    engine.rootContext()->setContextProperty("TitleScanner", titleScanner);

    auto emulatorLauncher = new EmulatorLauncher(&app);
    emulatorLauncher->connectInput(inputManager, inputProfileManager);
    engine.rootContext()->setContextProperty("EmulatorLauncher", emulatorLauncher);

    const QString gamesPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../games");
    titleScanner->scanDirectory(gamesPath);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl("qrc:/assets/qml/Main.qml"));

    auto *rootWindow = qobject_cast<QWindow *>(engine.rootObjects().first());
    auto *vulkanWindow = new VulkanOutputWindow();
    vulkanWindow->setTitle("WEMU");

    emulatorLauncher->setVulkanOutput(vulkanWindow);

    QObject::connect(emulatorLauncher, &EmulatorLauncher::stateChanged,
                     rootWindow, [rootWindow, vulkanWindow](bool running) {
        if (running) {
            vulkanWindow->resize(rootWindow->size());
            vulkanWindow->setPosition(rootWindow->position());
        } else {
            vulkanWindow->hide();
        }
    });

    QObject::connect(emulatorLauncher, &EmulatorLauncher::rendererReady,
                     rootWindow, [vulkanWindow]() {
        vulkanWindow->show();
        vulkanWindow->raise();
        vulkanWindow->requestActivate();
    });

    QObject::connect(vulkanWindow, &VulkanOutputWindow::escapePressed,
                     emulatorLauncher, &EmulatorLauncher::stop);

    QObject::connect(keyboard, &KeyboardInput::keyStateChanged,
                     emulatorLauncher, [emulatorLauncher](const QString &key, bool pressed) {
        if (pressed && key == "Escape")
            emulatorLauncher->stop();
    });

    auto *rootQuickWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(rootQuickWindow, &QQuickWindow::closing,
                     &app, [](auto *) { QCoreApplication::exit(0); });

    QObject::connect(&app, &QGuiApplication::aboutToQuit,
                     inputManager, &InputManager::stopPolling);

    QObject::connect(&app, &QGuiApplication::aboutToQuit,
                     emulatorLauncher, &EmulatorLauncher::stop);

    qDebug() << "Emulator GUI started with persistent InputManager";

    const int ret = app.exec();

    delete emulatorLauncher;   // destroys the Renderer
    delete vulkanWindow;       // destroys the Vulkan surface + instance
    delete inputManager;       // closes SDL game controllers
    SDL_Quit();
    return ret;
}
