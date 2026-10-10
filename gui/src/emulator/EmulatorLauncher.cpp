#include "EmulatorLauncher.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include "LaunchEnvironment.hpp"
#include "frontend/QtSessionProtocol.hpp"

namespace {
    quint32 buttonForKey(int key)
    {
        switch (key) {
            case Qt::Key_Up:
            case Qt::Key_W:
                return 0x0200;
            case Qt::Key_Down:
            case Qt::Key_S:
                return 0x0100;
            case Qt::Key_Left:
            case Qt::Key_A:
                return 0x0800;
            case Qt::Key_Right:
            case Qt::Key_D:
                return 0x0400;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                return 0x8000;
            case Qt::Key_Backspace:
                return 0x4000;
            case Qt::Key_P:
                return 0x0008;
            case Qt::Key_M:
                return 0x0004;
            default:
                return 0;
        }
    }
} // namespace

EmulatorLauncher::EmulatorLauncher(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (m_socket || !running()) {
                socket->deleteLater();
                continue;
            }
            m_socket = socket;
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (socket == m_socket)
                    readChannel();
            });
            readChannel();
        }
    });
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(2000);
    connect(&m_stopTimer, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::stateChanged, this, [this]() {
        if (!running())
            clearChannel();
        emit runningChanged();
        emit stateChanged(running());
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_stopping)
            setError(m_process.errorString());
        if (!running())
            clearChannel();
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        qInfo("[SESSION_EXIT] code=%d status=%d requested=%d", code, int(status), int(m_stopping));
        m_stopTimer.stop();
        if (!m_stopping && (code != 0 || status != QProcess::NormalExit))
            setError(QStringLiteral("Emulation stopped (exit %1). See the session log.").arg(code));
        m_stopping = false;
        emit sessionChanged();
    });
}

EmulatorLauncher::~EmulatorLauncher()
{
    if (running()) {
        stop();
        if (!m_process.waitForFinished(2000)) {
            m_process.terminate();
            if (m_process.waitForFinished(500))
                return;
            m_process.kill();
            m_process.waitForFinished();
        }
    }
}

void EmulatorLauncher::setError(const QString &error)
{
    m_error = error;
    emit errorChanged();
}

void EmulatorLauncher::launch(const QString &rpxPath, const QString &title, const QString &contentPath)
{
    if (running())
        return;
    setError({});
    m_stopping = false;
    clearChannel();
    m_title = title;
    m_showFps = false;
    if (!QFileInfo(rpxPath).isFile() || !QDir(contentPath).exists()) {
        setError("Game files are missing. Refresh the library.");
        return;
    }
    // The configured build core takes priority over old copied executables.
    QString binary = QString::fromUtf8(WEMU_CORE_BINARY);
    if (!QFileInfo(binary).isExecutable())
        binary = QDir(QCoreApplication::applicationDirPath()).filePath("../wemu");
    if (!QFileInfo(binary).isExecutable())
        binary = QDir(QCoreApplication::applicationDirPath()).filePath("wemu");
    if (!QFileInfo(binary).isExecutable()) {
        setError("The WEMU core executable was not found.");
        return;
    }
    const auto font = QSettings().value("emulation/sharedFont").toString();
    const auto environment = desktopLaunchEnvironment(QProcessEnvironment::systemEnvironment(), contentPath, font);
    const auto logs = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    if (!QDir().mkpath(logs)) {
        setError("Cannot create the session log directory.");
        return;
    }
    m_logPath = logs + "/session-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".log";
    emit logPathChanged();
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(QFileInfo(rpxPath).absolutePath());
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.setStandardOutputFile(m_logPath);
    const auto channel = QStringLiteral("wemu-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!m_server.listen(channel)) {
        setError("Cannot open the game display channel: " + m_server.errorString());
        return;
    }
    emit sessionChanged();
    m_process.start(binary, {"--gui-session", rpxPath, "--window-channel", m_server.fullServerName()});
}

void EmulatorLauncher::stop()
{
    if (!running())
        return;
    m_stopping = true;
    sendCommand(3);
    if (!m_connected)
        m_process.terminate();
    m_stopTimer.start();
    emit sessionChanged();
}

void EmulatorLauncher::openLog()
{
    if (!m_logPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_logPath));
}

void EmulatorLauncher::clearChannel()
{
    m_server.close();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_incoming.clear();
    m_connected = m_hasFrame = m_paused = m_pausePending = false;
    m_presentFps = m_movieFps = -1;
    m_awaitingFrame = m_lastFrame = 0;
    m_frames.clear();
    m_buttons = 0;
    m_heldKeys.clear();
    m_suppressedKeys.clear();
    emit frameReady({}, 0);
    emit sessionChanged();
    emit statisticsChanged();
}

void EmulatorLauncher::sendCommand(quint32 action, quint32 value)
{
    if (!m_socket || !m_connected)
        return;
    QByteArray body;
    QDataStream stream(&body, QIODevice::WriteOnly);
    WemuSession::configure(stream);
    stream << quint32(WemuSession::Type::Command) << action << value;
    m_socket->write(WemuSession::packet(body));
    m_socket->flush();
}

void EmulatorLauncher::acknowledgeFrame(quint64 sequence)
{
    if (!m_socket || sequence != m_awaitingFrame || !sequence)
        return;
    QByteArray body;
    QDataStream stream(&body, QIODevice::WriteOnly);
    WemuSession::configure(stream);
    stream << quint32(WemuSession::Type::FrameAck) << sequence;
    m_socket->write(WemuSession::packet(body));
    m_socket->flush();
    m_awaitingFrame = 0;
    dispatchFrame();
}

void EmulatorLauncher::dispatchFrame()
{
    if (m_awaitingFrame || m_frames.empty())
        return;
    auto frame = std::move(m_frames.front());
    m_frames.pop_front();
    m_awaitingFrame = frame.sequence;
    m_presentFps = frame.presentFps;
    m_movieFps = frame.movieFps;
    m_hasFrame = true;
    emit statisticsChanged();
    emit sessionChanged();
    emit frameReady(frame.image, frame.sequence);
}

void EmulatorLauncher::readChannel()
{
    if (!m_socket)
        return;
    try {
        m_incoming += m_socket->readAll();
        if (m_incoming.size() > 2ull * WemuSession::maximumMessage + 1024)
            throw std::runtime_error("Oversized game display buffer");
        QByteArray body;
        while (WemuSession::take(m_incoming, body)) {
            QDataStream stream(body);
            WemuSession::configure(stream);
            quint32 type{};
            stream >> type;
            if (type == quint32(WemuSession::Type::Hello)) {
                quint32 version{};
                stream >> version;
                if (m_connected || version != WemuSession::version || !stream.atEnd())
                    throw std::runtime_error("Incompatible game display protocol");
                m_connected = true;
                emit sessionChanged();
            } else if (type == quint32(WemuSession::Type::Frame) && m_connected) {
                quint64 sequence{}, presents{}, movies{};
                quint32 width{}, height{};
                double presentFps{}, movieFps{};
                stream >> sequence >> width >> height >> presentFps >> movieFps >> presents >> movies;
                const auto offset = stream.device()->pos();
                if (m_frames.size() + unsigned(m_awaitingFrame != 0) >= 2 || sequence != m_lastFrame + 1 || !width || !height || width > 4096 ||
                    height > 4096 || std::uint64_t(width) * height * 4 != std::uint64_t(body.size() - offset))
                    throw std::runtime_error("Invalid game framebuffer");
                const QImage borrowed(reinterpret_cast<const uchar *>(body.constData() + offset), int(width), int(height), int(width * 4),
                                      QImage::Format_RGBX8888);
                const auto image = borrowed.copy();
                if (image.isNull())
                    throw std::runtime_error("Cannot allocate the game display image");
                m_lastFrame = sequence;
                m_frames.push_back({image, sequence, presentFps, movieFps});
                dispatchFrame();
                continue; // Pixel bytes are intentionally outside the metadata stream.
            } else if (type == quint32(WemuSession::Type::PauseState) && m_connected) {
                quint32 paused{};
                quint64 presents{}, movies{};
                stream >> paused >> presents >> movies;
                if (paused > 1)
                    throw std::runtime_error("Invalid pause acknowledgement");
                m_paused = paused;
                m_pausePending = false;
                if (!paused)
                    m_presentFps = m_movieFps = -1;
                emit statisticsChanged();
                emit sessionChanged();
            } else
                throw std::runtime_error("Unexpected game display message");
            if (stream.status() != QDataStream::Ok || !stream.atEnd())
                throw std::runtime_error("Truncated game display message");
        }
    } catch (const std::exception &error) {
        setError(QString::fromUtf8(error.what()));
        stop();
    }
}

void EmulatorLauncher::togglePause()
{
    if (!m_connected || m_pausePending || m_stopping)
        return;
    if (m_paused) {
        resume();
        return;
    }
    m_suppressedKeys.unite(m_heldKeys);
    m_pausePending = true;
    m_buttons = 0;
    sendCommand(4, 0);
    sendCommand(1);
    emit sessionChanged();
}

void EmulatorLauncher::resume()
{
    if (!m_paused || m_pausePending || m_stopping)
        return;
    m_suppressedKeys.unite(m_heldKeys);
    m_pausePending = true;
    sendCommand(2);
    emit sessionChanged();
}

void EmulatorLauncher::toggleFps()
{
    m_showFps = !m_showFps;
    emit sessionChanged();
}

void EmulatorLauncher::sendButtons()
{
    quint32 buttons{};
    if (!m_paused && !m_pausePending && !m_stopping)
        for (const auto key: m_heldKeys)
            if (!m_suppressedKeys.contains(key))
                buttons |= buttonForKey(key);
    if (buttons != m_buttons) {
        m_buttons = buttons;
        sendCommand(4, buttons);
    }
}

bool EmulatorLauncher::eventFilter(QObject *object, QEvent *event)
{
    if (running() && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (qEnvironmentVariableIsSet("QT_WEMU_INPUT_TRACE") &&
            (key->key() == Qt::Key_O || key->key() == Qt::Key_Return || key->key() == Qt::Key_Escape))
            qInfo("[SESSION_KEY] type=%d key=%d repeat=%d target=%s paused=%d pending=%d", int(event->type()), key->key(), int(key->isAutoRepeat()),
                  object->metaObject()->className(), int(m_paused), int(m_pausePending));
        if (key->isAutoRepeat())
            return key->key() == Qt::Key_O;
        if (event->type() == QEvent::KeyPress) {
            const bool alreadyHeld = m_heldKeys.contains(key->key());
            m_heldKeys.insert(key->key());
            if (m_paused || m_pausePending)
                m_suppressedKeys.insert(key->key());
            if (key->key() == Qt::Key_O) {
                if (!alreadyHeld)
                    togglePause();
                return true;
            }
        } else {
            m_heldKeys.remove(key->key());
            m_suppressedKeys.remove(key->key());
        }
        sendButtons();
    } else if (running() && event->type() == QEvent::ApplicationDeactivate) {
        m_heldKeys.clear();
        m_suppressedKeys.clear();
        sendButtons();
    }
    return QObject::eventFilter(object, event);
}
