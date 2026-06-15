#pragma once
#include <QObject>
#include <QThread>
#include <qwindowdefs.h>
#include <atomic>
#include <memory>

namespace Core { class Interpreter; }
class Renderer;
class InputManager;
class InputProfileManager;
class VulkanOutputWindow;


class GameThread : public QThread {
    Q_OBJECT
public:
    explicit GameThread(QObject *parent = nullptr);
    ~GameThread() override;

    void queueGame(const QString &rpxPath, const QString &title);
    void stopGame();
    void setControllerMask(std::uint32_t mask);
    void setVulkanOutput(VulkanOutputWindow *window);

signals:
    void gameFinished();
    void gameError(const QString &message);
    void rendererReady();

protected:
    void run() override;

private:
    std::atomic<bool>                m_startRequested{false};
    std::atomic<Core::Interpreter *> m_interpreter{nullptr};
    QString                          m_nextPath;
    QString                          m_nextTitle;
    VulkanOutputWindow              *m_vulkanOutput = nullptr;
    std::unique_ptr<Renderer>        m_renderer;
};

class EmulatorLauncher : public QObject {
    Q_OBJECT
public:
    explicit EmulatorLauncher(QObject *parent = nullptr);
    ~EmulatorLauncher() override;

    Q_INVOKABLE void setWindowHandle(WId handle);
    void setVulkanOutput(VulkanOutputWindow *window);

    Q_INVOKABLE void launch(const QString &rpxPath, const QString &title = QString());
    Q_INVOKABLE void stop();

    void connectInput(InputManager *mgr, InputProfileManager *profileMgr);

signals:
    void stateChanged(bool running);
    void rendererReady();

private:
    GameThread         *m_thread       = nullptr;
    VulkanOutputWindow *m_vulkanWindow = nullptr;
    bool                m_emulating    = false;
};
