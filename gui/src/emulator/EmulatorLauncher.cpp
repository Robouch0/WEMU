#include "EmulatorLauncher.hpp"

#include "VulkanOutputWindow.hpp"
#include "binary/Loader.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "gfx/Renderer.hpp"
#include "hle/Coreinit.hpp"
#include "hle/Libc.hpp"
#include "hle/StdlibHooks.hpp"
#include "hle/Whb.hpp"
#include "InputManager.hpp"
#include "InputProfileManager.hpp"

#include <QDebug>
#include <mutex>

static void registerHleFunctionsOnce()
{
    static std::once_flag s_flag;
    std::call_once(s_flag, []() {
        RegisterCoreinitFunctions();
        RegisterLibcFunctions();
        RegisterWhbFunctions();
    });
}

GameThread::GameThread(QObject *parent) : QThread(parent) {}

GameThread::~GameThread() = default;

void GameThread::queueGame(const QString &rpxPath, const QString &title)
{
    m_nextPath = rpxPath;
    m_nextTitle = title;
    m_startRequested = true;
}

void GameThread::stopGame()
{
    if (auto *interp = m_interpreter.load())
        interp->stop();
}

void GameThread::setControllerMask(std::uint32_t mask)
{
    if (auto *interp = m_interpreter.load())
        interp->m_controllerMask = mask;
}

void GameThread::setVulkanOutput(VulkanOutputWindow *window)
{
    m_vulkanOutput = window;
    m_renderer = std::make_unique<Renderer>(
        window->vkInstance(),
        window->vkSurface(),
        static_cast<uint32_t>(window->width()),
        static_cast<uint32_t>(window->height())
    );
}

void GameThread::run()
{
    registerHleFunctionsOnce();

    while (!isInterruptionRequested()) {
        if (!m_startRequested) {
            msleep(10);
            continue;
        }

        QString path  = m_nextPath;
        QString title = m_nextTitle;
        m_startRequested = false;

        if (!m_renderer) {
            m_renderer = std::make_unique<Renderer>();
        }

        m_renderer->setFirstFrameCallback([this]() { emit rendererReady(); });

        try {
            const Core::Loader loader(path.toStdString());
            Core::Binary binary = loader.getBinary();

            Core::Interpreter interpreter(binary);
            m_interpreter = &interpreter;

            interpreter.m_renderer = m_renderer.get();
            Core::installStdlibHooks(interpreter, binary);

            interpreter.m_gpr[1] = 0xC0FFFFF0u;
            interpreter.m_gpr[2] = 0u;
            interpreter.m_gpr[3] = 0u;
            interpreter.m_gpr[4] = 0u;
            interpreter.m_memory.write<uint32_t>(0xC0FFFFF0u, 0);

            interpreter.run();

            m_interpreter = nullptr;

        } catch (const std::exception &e) {
            m_interpreter = nullptr;
            emit gameError(QString::fromStdString(e.what()));
        }

        emit gameFinished();
    }
}

EmulatorLauncher::EmulatorLauncher(QObject *parent)
    : QObject(parent)
{
    m_thread = new GameThread(this);

    connect(m_thread, &GameThread::gameFinished, this, [this]() {
        m_emulating = false;
        emit stateChanged(false);
    });
    connect(m_thread, &GameThread::gameError, this, [](const QString &msg) {
        qWarning() << "[Launcher] Emulator error:" << msg;
    });
    connect(m_thread, &GameThread::rendererReady, this, &EmulatorLauncher::rendererReady);

    m_thread->start();
}

EmulatorLauncher::~EmulatorLauncher()
{
    m_thread->stopGame();
    m_thread->requestInterruption();
    m_thread->wait();
}

void EmulatorLauncher::setWindowHandle(WId handle)
{
    Q_UNUSED(handle)
}

void EmulatorLauncher::setVulkanOutput(VulkanOutputWindow *window)
{
    m_vulkanWindow = window;
    m_thread->setVulkanOutput(window);
}

void EmulatorLauncher::launch(const QString &rpxPath, const QString &title)
{
    if (m_emulating) {
        qWarning() << "[Launcher] Already running, ignoring launch request";
        return;
    }

    if (m_vulkanWindow)
        m_vulkanWindow->setTitle(title.isEmpty() ? QStringLiteral("WEMU")
                                                 : QStringLiteral("WEMU — ") + title);

    m_emulating = true;
    m_thread->queueGame(rpxPath, title);
    emit stateChanged(true);
}

void EmulatorLauncher::stop()
{
    m_thread->stopGame();
}

void EmulatorLauncher::connectInput(InputManager *mgr, InputProfileManager *profileMgr)
{
    static constexpr std::uint32_t VPAD_UP    = 0x0200;
    static constexpr std::uint32_t VPAD_DOWN  = 0x0100;
    static constexpr std::uint32_t VPAD_LEFT  = 0x0800;
    static constexpr std::uint32_t VPAD_RIGHT = 0x0400;
    static constexpr std::uint32_t VPAD_A     = 0x8000;
    static constexpr std::uint32_t VPAD_B     = 0x4000;
    static constexpr std::uint32_t VPAD_X     = 0x2000;
    static constexpr std::uint32_t VPAD_Y     = 0x1000;
    static constexpr std::uint32_t VPAD_L     = 0x0010;
    static constexpr std::uint32_t VPAD_R     = 0x0020;
    static constexpr std::uint32_t VPAD_ZL    = 0x0001;
    static constexpr std::uint32_t VPAD_ZR    = 0x0002;
    static constexpr std::uint32_t VPAD_PLUS  = 0x0008;
    static constexpr std::uint32_t VPAD_MINUS = 0x0004;

    connect(mgr, &InputManager::inputUpdated, this, [this, mgr, profileMgr]() {
        auto pressed = [&](const QString &wiiuAction) {
            return mgr->isButtonPressed(profileMgr->getBinding(wiiuAction));
        };

        auto key = [&](const QString &keyName) {
            return mgr->isButtonPressed(keyName);
        };

        std::uint32_t mask = 0;
        if (pressed("DPAD_UP")    || key("Up"))    mask |= VPAD_UP;
        if (pressed("DPAD_DOWN")  || key("Down"))  mask |= VPAD_DOWN;
        if (pressed("DPAD_LEFT")  || key("Left"))  mask |= VPAD_LEFT;
        if (pressed("DPAD_RIGHT") || key("Right")) mask |= VPAD_RIGHT;

        if (pressed("A") || key("Return") || key("Enter")) mask |= VPAD_A;
        if (pressed("B") || key("Backspace"))      mask |= VPAD_B;
        if (pressed("X"))          mask |= VPAD_X;
        if (pressed("Y"))          mask |= VPAD_Y;
        if (pressed("L"))          mask |= VPAD_L;
        if (pressed("R"))          mask |= VPAD_R;
        if (pressed("ZL"))         mask |= VPAD_ZL;
        if (pressed("ZR"))         mask |= VPAD_ZR;
        if (mgr->isButtonPressed("START") || key("P")) mask |= VPAD_PLUS;
        if (mgr->isButtonPressed("BACK")  || key("M")) mask |= VPAD_MINUS;
        m_thread->setControllerMask(mask);
    });
}
