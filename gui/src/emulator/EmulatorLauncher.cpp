#include "EmulatorLauncher.hpp"

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
#include <optional>

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

void GameThread::run()
{
    registerHleFunctionsOnce();

    std::optional<Renderer> renderer;

    while (!isInterruptionRequested()) {
        if (!m_startRequested) {
            msleep(10);
            continue;
        }

        QString path  = m_nextPath;
        QString title = m_nextTitle;
        m_startRequested = false;

        if (!renderer.has_value())
            renderer.emplace();

        try {
            const Core::Loader loader(path.toStdString());
            Core::Binary binary = loader.getBinary();

            Core::Interpreter interpreter(binary);
            m_interpreter = &interpreter;

            interpreter.m_renderer = &*renderer;
            Core::installStdlibHooks(interpreter, binary);

            interpreter.m_gpr[1] = 0xC0FFFFF0u;
            interpreter.m_gpr[2] = 0u;
            interpreter.m_gpr[3] = 0u;
            interpreter.m_gpr[4] = 0u;
            interpreter.m_memory.write<uint32_t>(0xC0FFFFF0u, 0);

            renderer->set_title(title.isEmpty() ? "WEMU" : title.toStdString());
            renderer->show();
            interpreter.run();
            renderer->hide();

            m_interpreter = nullptr;

        } catch (const std::exception &e) {
            if (renderer.has_value()) renderer->hide();
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

void EmulatorLauncher::launch(const QString &rpxPath, const QString &title)
{
    if (m_emulating) {
        qWarning() << "[Launcher] Already running, ignoring launch request";
        return;
    }

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

        std::uint32_t mask = 0;
        if (pressed("DPAD_UP"))    mask |= VPAD_UP;
        if (pressed("DPAD_DOWN"))  mask |= VPAD_DOWN;
        if (pressed("DPAD_LEFT"))  mask |= VPAD_LEFT;
        if (pressed("DPAD_RIGHT")) mask |= VPAD_RIGHT;
        if (pressed("A"))          mask |= VPAD_A;
        if (pressed("B"))          mask |= VPAD_B;
        if (pressed("X"))          mask |= VPAD_X;
        if (pressed("Y"))          mask |= VPAD_Y;
        if (pressed("L"))          mask |= VPAD_L;
        if (pressed("R"))          mask |= VPAD_R;
        if (pressed("ZL"))         mask |= VPAD_ZL;
        if (pressed("ZR"))         mask |= VPAD_ZR;
        if (mgr->isButtonPressed("START")) mask |= VPAD_PLUS;
        if (mgr->isButtonPressed("BACK"))  mask |= VPAD_MINUS;
        m_thread->setControllerMask(mask);
    });
}
