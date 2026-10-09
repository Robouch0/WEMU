#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>

class EmulatorLauncher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString logPath READ logPath NOTIFY logPathChanged)
public:
    explicit EmulatorLauncher(QObject *parent = nullptr);
    ~EmulatorLauncher() override;
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    QString error() const { return m_error; }
    QString logPath() const { return m_logPath; }
    Q_INVOKABLE void launch(const QString &rpxPath, const QString &title, const QString &contentPath);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openLog();
signals:
    void runningChanged();
    void stateChanged(bool running);
    void errorChanged();
    void logPathChanged();
private:
    QProcess m_process;
    QTimer m_stopTimer;
    QString m_error, m_logPath;
    bool m_stopping = false;
    void setError(const QString &error);
};
