#pragma once
#include <QObject>
#include <QThread>
#include <qwindowdefs.h>
#include <atomic>
#include <optional>

namespace Core { class Interpreter; }
class Renderer;
class InputManager;
class InputProfileManager;


class GameThread : public QThread {
    Q_OBJECT
public:
    explicit GameThread(QObject *parent = nullptr);

    void queueGame(const QString &rpxPath, const QString &title);
    void stopGame();
    void setControllerMask(std::uint32_t mask);

signals:
    void gameFinished();
    void gameError(const QString &message);

protected:
    void run() override;

private:
    std::atomic<bool>                m_startRequested{false};
    std::atomic<Core::Interpreter *> m_interpreter{nullptr};
    QString                          m_nextPath;
    QString                          m_nextTitle;
};

class EmulatorLauncher : public QObject {
    Q_OBJECT
public:
    explicit EmulatorLauncher(QObject *parent = nullptr);
    ~EmulatorLauncher() override;

    Q_INVOKABLE void setWindowHandle(WId handle);

    Q_INVOKABLE void launch(const QString &rpxPath, const QString &title = QString());
    Q_INVOKABLE void stop();

    void connectInput(InputManager *mgr, InputProfileManager *profileMgr);

signals:
    void stateChanged(bool running);

private:
    GameThread *m_thread    = nullptr;
    bool        m_emulating = false;
};
