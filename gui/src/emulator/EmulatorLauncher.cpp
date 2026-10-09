#include "EmulatorLauncher.hpp"
#include "LaunchEnvironment.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

EmulatorLauncher::EmulatorLauncher(QObject *parent) : QObject(parent)
{
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(2000);
    connect(&m_stopTimer, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::stateChanged, this, [this]() {
        emit runningChanged();
        emit stateChanged(running());
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_stopping) setError(m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_stopTimer.stop();
        if (!m_stopping && (code != 0 || status != QProcess::NormalExit))
            setError(QStringLiteral("Emulation stopped (exit %1). See the session log.").arg(code));
        m_stopping = false;
    });
}

EmulatorLauncher::~EmulatorLauncher()
{
    if (running()) {
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) { m_process.kill(); m_process.waitForFinished(); }
    }
}

void EmulatorLauncher::setError(const QString &error) { m_error = error; emit errorChanged(); }

void EmulatorLauncher::launch(const QString &rpxPath, const QString &, const QString &contentPath)
{
    if (running()) return;
    setError({});
    m_stopping = false;
    if (!QFileInfo(rpxPath).isFile() || !QDir(contentPath).exists()) {
        setError("Game files are missing. Refresh the library.");
        return;
    }
    QString binary = QDir(QCoreApplication::applicationDirPath()).filePath("../wemu");
    if (!QFileInfo(binary).isExecutable()) binary = QDir(QCoreApplication::applicationDirPath()).filePath("wemu");
    if (!QFileInfo(binary).isExecutable()) binary = QString::fromUtf8(WEMU_CORE_BINARY);
    if (!QFileInfo(binary).isExecutable()) { setError("The WEMU core executable was not found."); return; }
    const auto font = QSettings().value("emulation/sharedFont").toString();
    const auto environment = desktopLaunchEnvironment(QProcessEnvironment::systemEnvironment(), contentPath, font);
    const auto logs = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    if (!QDir().mkpath(logs)) { setError("Cannot create the session log directory."); return; }
    m_logPath = logs + "/session-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".log";
    emit logPathChanged();
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(QFileInfo(rpxPath).absolutePath());
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.setStandardOutputFile(m_logPath);
    m_process.start(binary, {"--gui-session", rpxPath});
}

void EmulatorLauncher::stop()
{
    if (!running()) return;
    m_stopping = true;
    m_process.terminate();
    m_stopTimer.start();
}

void EmulatorLauncher::openLog() { if (!m_logPath.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_logPath)); }
