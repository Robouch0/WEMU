#pragma once
#include <QObject>
#include <QThread>
#include <qwindowdefs.h>
#include <atomic>
#include <optional>

namespace Core { class Interpreter; }
class Renderer;


class GameThread : public QThread {
    Q_OBJECT
public:
    explicit GameThread(QObject *parent = nullptr);

    void queueGame(const QString &rpxPath);

    void stopGame();

signals:
    void gameFinished();
    void gameError(const QString &message);

protected:
    void run() override;

private:
    std::atomic<bool>                m_startRequested{false};
    std::atomic<Core::Interpreter *> m_interpreter{nullptr};
    QString                          m_nextPath;
};

class EmulatorLauncher : public QObject {
    Q_OBJECT
public:
    explicit EmulatorLauncher(QObject *parent = nullptr);
    ~EmulatorLauncher() override;

    Q_INVOKABLE void setWindowHandle(WId handle);

    Q_INVOKABLE void launch(const QString &rpxPath);
    Q_INVOKABLE void stop();

signals:
    void stateChanged(bool running);

private:
    GameThread *m_thread    = nullptr;
    bool        m_emulating = false;
};
