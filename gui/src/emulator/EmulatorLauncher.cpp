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

#include "ControllerMapping.hpp"
#include "LaunchEnvironment.hpp"
#include "LocaleSettings.hpp"
#include "SessionFrame.hpp"
#include "frontend/QtSessionProtocol.hpp"

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
    // Coalesce analog input (gyro changes every sample) to one core message per display frame.
    m_gamepadTimer.setSingleShot(true);
    m_gamepadTimer.setInterval(8);
    connect(&m_gamepadTimer, &QTimer::timeout, this, &EmulatorLauncher::sendGamepad);
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

int EmulatorLauncher::systemLanguage() const
{
    bool valid{};
    const auto value = QSettings().value("emulation/systemLanguage", 1).toInt(&valid);
    return valid && Core::SystemConfig::validLanguage(unsigned(value)) ? value : 1;
}

int EmulatorLauncher::systemRegion() const
{
    bool valid{};
    const auto value = QSettings().value("emulation/systemRegion", 0).toInt(&valid);
    return valid && (value == 0 || Core::SystemConfig::validRegion(unsigned(value))) ? value : 0;
}

void EmulatorLauncher::setSystemLanguage(int value)
{
    if (!Core::SystemConfig::validLanguage(unsigned(value)) || value == systemLanguage()) return;
    QSettings().setValue("emulation/systemLanguage", value);
    emit settingsChanged();
}

void EmulatorLauncher::setSystemRegion(int value)
{
    if ((value != 0 && !Core::SystemConfig::validRegion(unsigned(value))) || value == systemRegion()) return;
    QSettings().setValue("emulation/systemRegion", value);
    emit settingsChanged();
}

void EmulatorLauncher::launch(const QString &rpxPath, const QString &title, const QString &contentPath)
{
    if (running())
        return;
    setError({});
    m_stopping = false;
    clearChannel();
    m_title = title;
    m_rpxPath = rpxPath;
    m_showFps = false;
    m_suppressedControllerButtons = m_controllerButtons;
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
    bool nativeCpu = false;
#if defined(WEMU_GUI_HAS_LLVM) || defined(WEMU_GUI_HAS_X64_JIT)
    nativeCpu = QSettings().value("emulation/nativeCpu", true).toBool();
#endif
    bool x64Cpu = false;
#ifdef WEMU_GUI_HAS_X64_JIT
    // Prefer the complete chained backend when this core includes it. The core
    // checks host instruction support and retains interpreter fallback.
    x64Cpu = nativeCpu;
#endif
    QString nativeCache;
    if (nativeCpu && !x64Cpu) {
        const auto cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        // ObjectCache creates/checks the leaf directory with private permissions.
        // A missing/unusable cache falls back to JIT; it never prevents launch.
        if (!cacheRoot.isEmpty() && QDir().mkpath(cacheRoot)) nativeCache = cacheRoot + "/ppc-native";
    }
    const auto titleRegion = titleRegionForLaunch(rpxPath);
    const auto region = systemRegion() ? unsigned(systemRegion()) : titleRegion;
    const auto environment = desktopLaunchEnvironment(QProcessEnvironment::systemEnvironment(), contentPath, font,
                                                     nativeCpu, nativeCache, unsigned(systemLanguage()), region, titleRegion, x64Cpu);
    qInfo() << "[SESSION_CPU]" << (x64Cpu ? "x86-64 JIT with interpreter fallback" : nativeCpu ? "LLVM with interpreter fallback" : "interpreter")
            << "cache=" << nativeCache;
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
    m_remoteButtons = 0;
    m_remoteGamepad = {};
    m_keyboardLeftStick = {};
    m_gamepadDirty = false;
    m_gamepadTimer.stop();
    m_heldKeys.clear();
    m_suppressedKeys.clear();
    emit frameReady({}, 0);
    emit drcFrameReady({});
    emit sessionChanged();
    emit statisticsChanged();
}

void EmulatorLauncher::sendCommand(quint32 action, quint32 value)
{
    if (!m_socket || !m_connected)
        return;
    if (action == 4 && qEnvironmentVariableIntValue("QT_WEMU_INPUT_TRACE") == 1)
        qInfo("[SESSION_BUTTONS] mask=%08x", value);
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
                if (m_drcStreaming)
                    sendCommand(5, 1);
                m_gamepadDirty = true;
                sendGamepad();
                emit sessionChanged();
            } else if (type == quint32(WemuSession::Type::Frame) && m_connected) {
                quint64 sequence{}, presents{}, movies{};
                quint32 width{}, height{};
                double presentFps{}, movieFps{};
                stream >> sequence >> width >> height >> presentFps >> movieFps >> presents >> movies;
                const auto offset = stream.device()->pos();
                if (stream.status() != QDataStream::Ok || m_frames.size() + unsigned(m_awaitingFrame != 0) >= 2 || sequence != m_lastFrame + 1 ||
                    !width || !height || width > 4096 || height > 4096 || std::uint64_t(width) * height * 4 != std::uint64_t(body.size() - offset))
                    throw std::runtime_error("Invalid game framebuffer");
                const auto image = WemuSession::frameImage(std::move(body), offset, width, height);
                if (qEnvironmentVariableIntValue("QT_WEMU_INPUT_TRACE") == 1 &&
                    (!m_hasFrame || sequence % 300 == 0 || (movieFps < 0 && m_movieFps >= 0)))
                    qInfo("[SESSION_FRAME] presents=%llu movies=%llu present_fps=%.2f movie_fps=%.2f",
                          static_cast<unsigned long long>(presents), static_cast<unsigned long long>(movies), presentFps, movieFps);
                body = {};
                m_lastFrame = sequence;
                m_frames.push_back({image, sequence, presentFps, movieFps});
                dispatchFrame();
                continue; // Pixel bytes are intentionally outside the metadata stream.
            } else if (type == quint32(WemuSession::Type::DrcFrame) && m_connected) {
                quint32 width{}, height{};
                stream >> width >> height;
                const auto offset = stream.device()->pos();
                if (stream.status() != QDataStream::Ok || !width || !height || width > 4096 || height > 4096 ||
                    std::uint64_t(width) * height * 4 != std::uint64_t(body.size() - offset))
                    throw std::runtime_error("Invalid GamePad framebuffer");
                emit drcFrameReady(WemuSession::frameImage(std::move(body), offset, width, height));
                body = {};
                continue;
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
    m_suppressedControllerButtons |= m_controllerButtons;
    m_pausePending = true;
    sendButtons();
    sendCommand(1);
    emit sessionChanged();
}

void EmulatorLauncher::resume()
{
    if (!m_paused || m_pausePending || m_stopping)
        return;
    m_suppressedKeys.unite(m_heldKeys);
    m_suppressedControllerButtons |= m_controllerButtons;
    m_pausePending = true;
    sendCommand(2);
    emit sessionChanged();
}

void EmulatorLauncher::toggleFps()
{
    m_showFps = !m_showFps;
    emit sessionChanged();
}

void EmulatorLauncher::setRemoteButtons(quint32 buttons)
{
    if (buttons == m_remoteButtons)
        return;
    m_remoteButtons = buttons;
    sendButtons();
}

void EmulatorLauncher::setRemoteGamepad(const Core::Gfx::GamepadState &state)
{
    const auto &a = state, &b = m_remoteGamepad;
    if (a.leftStick == b.leftStick && a.rightStick == b.rightStick && a.touched == b.touched && a.touchX == b.touchX && a.touchY == b.touchY &&
        a.accel == b.accel && a.gyro == b.gyro)
        return;
    m_remoteGamepad = state;
    m_gamepadDirty = true;
    if (!m_gamepadTimer.isActive())
        m_gamepadTimer.start();
}

void EmulatorLauncher::setControllerInput(quint32 buttons, const std::array<float, 2> &leftStick, const std::array<float, 2> &rightStick)
{
    // A press that pauses or resumes (the menu's A) must not reach the game afterwards.
    if (m_paused || m_pausePending)
        m_suppressedControllerButtons |= buttons;
    m_suppressedControllerButtons &= buttons;
    if (leftStick != m_controllerLeftStick || rightStick != m_controllerRightStick) {
        m_controllerLeftStick = leftStick;
        m_controllerRightStick = rightStick;
        m_gamepadDirty = true;
        if (!m_gamepadTimer.isActive())
            m_gamepadTimer.start();
    }
    if (buttons != m_controllerButtons) {
        m_controllerButtons = buttons;
        sendButtons();
    }
}

void EmulatorLauncher::sendGamepad()
{
    if (!m_socket || !m_connected || !m_gamepadDirty)
        return;
    m_gamepadDirty = false;
    // Touch and motion come from the phone only.
    auto gamepad = m_remoteGamepad;
    gamepad.leftStick = ControllerMapping::stronger(m_remoteGamepad.leftStick, m_controllerLeftStick);
    gamepad.leftStick = ControllerMapping::stronger(gamepad.leftStick, m_keyboardLeftStick);
    gamepad.rightStick = ControllerMapping::stronger(m_remoteGamepad.rightStick, m_controllerRightStick);
    if (qEnvironmentVariableIntValue("QT_WEMU_INPUT_TRACE") == 1)
        qInfo("[SESSION_GAMEPAD] left=%.2f,%.2f right=%.2f,%.2f touched=%d", gamepad.leftStick[0], gamepad.leftStick[1], gamepad.rightStick[0],
              gamepad.rightStick[1], int(gamepad.touched));
    QByteArray body;
    QDataStream stream(&body, QIODevice::WriteOnly);
    WemuSession::configure(stream);
    stream << quint32(WemuSession::Type::Gamepad);
    WemuSession::writeGamepad(stream, gamepad);
    m_socket->write(WemuSession::packet(body));
    m_socket->flush();
}

void EmulatorLauncher::setDrcStreaming(bool enabled)
{
    if (enabled == m_drcStreaming)
        return;
    m_drcStreaming = enabled;
    sendCommand(5, enabled ? 1 : 0);
}

void EmulatorLauncher::sendButtons()
{
    quint32 buttons{};
    const auto keyboard = ControllerMapping::keyboard([&](int key) {
        return !m_paused && !m_pausePending && !m_stopping && m_heldKeys.contains(key) && !m_suppressedKeys.contains(key);
    });
    if (keyboard.leftStick != m_keyboardLeftStick) {
        m_keyboardLeftStick = keyboard.leftStick;
        m_gamepadDirty = true;
        if (!m_gamepadTimer.isActive())
            m_gamepadTimer.start();
    }
    if (!m_paused && !m_pausePending && !m_stopping) {
        buttons = keyboard.buttons;
        buttons |= m_remoteButtons;
        buttons |= m_controllerButtons & ~m_suppressedControllerButtons;
    }
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
