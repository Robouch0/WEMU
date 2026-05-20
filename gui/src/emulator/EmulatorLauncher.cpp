#include "EmulatorLauncher.hpp"

#include "binary/Loader.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "gfx/Renderer.hpp"
#include "hle/Coreinit.hpp"
#include "hle/Libc.hpp"
#include "hle/Whb.hpp"

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

void GameThread::queueGame(const QString &rpxPath)
{
    m_nextPath = rpxPath;
    m_startRequested.store(true, std::memory_order_release);
}

void GameThread::stopGame()
{
    if (auto *interp = m_interpreter.load(std::memory_order_relaxed))
        interp->stop();
}

void GameThread::run()
{
    registerHleFunctionsOnce();

    std::optional<Renderer> renderer;

    while (!isInterruptionRequested()) {
        if (!m_startRequested.load(std::memory_order_acquire)) {
            msleep(10);
            continue;
        }

        QString path = m_nextPath;
        m_startRequested.store(false, std::memory_order_relaxed);

        // First launch: initialize SDL and create the SDL window (hidden).
        if (!renderer.has_value())
            renderer.emplace();

        try {
            qDebug() << "[Worker] loading" << path;
            const Core::Loader loader(path.toStdString());
            Core::Binary binary = loader.getBinary();

            qDebug() << "[Worker] building interpreter...";
            Core::Interpreter interpreter(binary);
            m_interpreter.store(&interpreter, std::memory_order_relaxed);

            interpreter.m_renderer = &*renderer;

            // stdlib hooks — bypass broken WUT sbrk heap (mirrors core/src/main.cpp)
            struct { const char *name; Core::Interpreter::HookFn fn; } hooks[] = {
                {"memalign",     [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], cpu.m_gpr[3] ? cpu.m_gpr[3] : 8); }},
                {"malloc",       [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[3], 8); }},
                {"calloc",       [](Core::Interpreter &cpu) {
                    uint32_t n = cpu.m_gpr[3] * cpu.m_gpr[4];
                    uint32_t a = cpu.m_memory.heapAllocate(n, 8);
                    if (a) { if (auto *p = cpu.m_memory.hostPtr(a)) std::memset(p, 0, n); }
                    cpu.m_gpr[3] = a;
                }},
                {"realloc",      [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 8); }},
                {"free",         [](Core::Interpreter &) {}},
                {"__wut_sbrk_r", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 4); }},
                {"_sbrk_r",      [](Core::Interpreter &cpu) { cpu.m_gpr[3] = cpu.m_memory.heapAllocate(cpu.m_gpr[4], 4); }},
            };
            for (auto &h : hooks)
                for (const auto &sym : binary.symbols)
                    if (sym.name == h.name && sym.raw.header.st_value >= 0x02000000u && sym.raw.header.st_value < 0x10000000u)
                        interpreter.m_hooks[sym.raw.header.st_value] = h.fn;

            interpreter.m_gpr[1] = 0xC0FFFFF0u;
            interpreter.m_gpr[2] = 0u;
            interpreter.m_gpr[3] = 0u;
            interpreter.m_gpr[4] = 0u;
            interpreter.m_memory.write<uint32_t>(0xC0FFFFF0u, 0);

            renderer->show();
            interpreter.run();
            renderer->hide();
            qDebug() << "[Worker] interpreter finished.";

            m_interpreter.store(nullptr, std::memory_order_relaxed);

        } catch (const std::exception &e) {
            if (renderer.has_value()) renderer->hide();
            m_interpreter.store(nullptr, std::memory_order_relaxed);
            qWarning() << "[Worker] exception:" << e.what();
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

void EmulatorLauncher::launch(const QString &rpxPath)
{
    if (m_emulating) {
        qWarning() << "[Launcher] Already running, ignoring launch request";
        return;
    }

    m_emulating = true;
    m_thread->queueGame(rpxPath);
    emit stateChanged(true);
}

void EmulatorLauncher::stop()
{
    m_thread->stopGame();
}
