#pragma once
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <deque>

class EmulatorLauncher : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool running READ running NOTIFY runningChanged)
        Q_PROPERTY(QString error READ error NOTIFY errorChanged)
        Q_PROPERTY(QString logPath READ logPath NOTIFY logPathChanged)
        Q_PROPERTY(bool connected READ connected NOTIFY sessionChanged)
        Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY sessionChanged)
        Q_PROPERTY(bool paused READ paused NOTIFY sessionChanged)
        Q_PROPERTY(bool pausePending READ pausePending NOTIFY sessionChanged)
        Q_PROPERTY(bool stopping READ stopping NOTIFY sessionChanged)
        Q_PROPERTY(bool showFps READ showFps NOTIFY sessionChanged)
        Q_PROPERTY(double presentFps READ presentFps NOTIFY statisticsChanged)
        Q_PROPERTY(double movieFps READ movieFps NOTIFY statisticsChanged)
        Q_PROPERTY(QString gameTitle READ gameTitle NOTIFY sessionChanged)
    public:
        explicit EmulatorLauncher(QObject *parent = nullptr);
        ~EmulatorLauncher() override;
        bool running() const { return m_process.state() != QProcess::NotRunning; }
        QString error() const { return m_error; }
        QString logPath() const { return m_logPath; }
        bool connected() const { return m_connected; }
        bool hasFrame() const { return m_hasFrame; }
        bool paused() const { return m_paused; }
        bool pausePending() const { return m_pausePending; }
        bool stopping() const { return m_stopping; }
        bool showFps() const { return m_showFps; }
        double presentFps() const { return m_presentFps; }
        double movieFps() const { return m_movieFps; }
        QString gameTitle() const { return m_title; }
        Q_INVOKABLE void launch(const QString &rpxPath, const QString &title, const QString &contentPath);
        Q_INVOKABLE void stop();
        Q_INVOKABLE void openLog();
        Q_INVOKABLE void togglePause();
        Q_INVOKABLE void resume();
        Q_INVOKABLE void toggleFps();
        void acknowledgeFrame(quint64 sequence);
    signals:
        void runningChanged();
        void stateChanged(bool running);
        void errorChanged();
        void logPathChanged();
        void sessionChanged();
        void statisticsChanged();
        void frameReady(const QImage &image, quint64 sequence);

    protected:
        bool eventFilter(QObject *object, QEvent *event) override;

    private:
        void readChannel();
        void sendCommand(quint32 action, quint32 value = 0);
        void clearChannel();
        void sendButtons();
        void dispatchFrame();
        QProcess m_process;
        QTimer m_stopTimer;
        QString m_error, m_logPath;
        bool m_stopping = false;
        QLocalServer m_server;
        QLocalSocket *m_socket{};
        QByteArray m_incoming;
        bool m_connected{}, m_hasFrame{}, m_paused{}, m_pausePending{}, m_showFps{};
        double m_presentFps{-1}, m_movieFps{-1};
        quint64 m_awaitingFrame{}, m_lastFrame{};
        struct PendingFrame {
                QImage image;
                quint64 sequence{};
                double presentFps{}, movieFps{};
        };
        std::deque<PendingFrame> m_frames;
        quint32 m_buttons{};
        QSet<int> m_heldKeys, m_suppressedKeys;
        QString m_title;
        void setError(const QString &error);
};
