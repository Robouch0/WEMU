#include "RemoteGamepadServer.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSettings>
#include <QSysInfo>
#include <QThreadPool>
#include <QUrl>
#include <QUuid>

#include "EmulatorLauncher.hpp"
#include "TitleScanner.hpp"

namespace {
    using RemoteProtocol::Type;

    constexpr qint64 maximumQueuedBytes = 384 * 1024; // keep latency low: never queue many frames
    constexpr int clientTimeoutMs = 6000;
    constexpr int maximumFailedPins = 5;
    constexpr int lockoutMs = 30000;

    QString tokenHash(const QString &token) { return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex(); }

    QString settingsKey(const QString &deviceId)
    {
        // Device ids come from the network: hash them into a safe settings key.
        return "remoteGamepad/devices/" + QCryptographicHash::hash(deviceId.toUtf8(), QCryptographicHash::Sha1).toHex();
    }
} // namespace

RemoteGamepadServer::RemoteGamepadServer(EmulatorLauncher &launcher, TitleScanner &library, QObject *parent) :
    QObject(parent), m_launcher(launcher), m_library(library)
{
    regeneratePin();
    connect(&m_server, &QTcpServer::newConnection, this, &RemoteGamepadServer::acceptClients);
    connect(&m_discovery, &QUdpSocket::readyRead, this, &RemoteGamepadServer::readDiscovery);

    // A phone that vanished from Wi-Fi never closes its socket: it pings every second.
    m_watchdog.setInterval(1000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (m_client && m_lastClientMessage.isValid() && m_lastClientMessage.elapsed() > clientTimeoutMs)
            dropClient("timeout");
        m_pending.removeIf([](const QPointer<QTcpSocket> &socket) { return !socket; });
    });

    connect(&m_launcher, &EmulatorLauncher::sessionChanged, this, [this] {
        sendSession();
        updateStreaming();
    });
    connect(&m_launcher, &EmulatorLauncher::errorChanged, this, [this] { sendSession(); });
    connect(&m_launcher, &EmulatorLauncher::drcFrameReady, this, [this](const QImage &image) {
        if (m_source == 0)
            offerFrame(image, 0);
    });
    connect(&m_launcher, &EmulatorLauncher::frameReady, this, [this](const QImage &image, quint64) {
        if (m_source == 1)
            offerFrame(image, 1);
    });
    connect(&m_library, &QAbstractItemModel::modelReset, this, &RemoteGamepadServer::sendLibrary);
    connect(&m_library, &QAbstractItemModel::rowsInserted, this, &RemoteGamepadServer::sendLibrary);
    connect(&m_library, &QAbstractItemModel::rowsRemoved, this, &RemoteGamepadServer::sendLibrary);

    setEnabled(QSettings().value("remoteGamepad/enabled", true).toBool());
}

RemoteGamepadServer::~RemoteGamepadServer()
{
    ++m_generation;
    stop();
}

void RemoteGamepadServer::setEnabled(bool enabled)
{
    if (enabled == m_enabled && enabled == listening())
        return;
    m_enabled = enabled;
    QSettings().setValue("remoteGamepad/enabled", enabled);
    enabled ? start() : stop();
    emit stateChanged();
}

void RemoteGamepadServer::start()
{
    m_error.clear();
    const auto port = quint16(QSettings().value("remoteGamepad/port", RemoteProtocol::defaultPort).toUInt());
    if (!m_server.isListening() && !m_server.listen(QHostAddress::AnyIPv4, port))
        m_error = QStringLiteral("Port %1 unavailable: %2").arg(port).arg(m_server.errorString());
    if (m_discovery.state() != QAbstractSocket::BoundState &&
        !m_discovery.bind(QHostAddress::AnyIPv4, RemoteProtocol::discoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
        qWarning("[REMOTE] discovery port %u unavailable: %s", RemoteProtocol::discoveryPort, qPrintable(m_discovery.errorString()));
    m_watchdog.start();
    if (m_server.isListening())
        qInfo("[REMOTE] GamePad server listening on port %u", m_server.serverPort());
}

void RemoteGamepadServer::stop()
{
    dropClient("disabled");
    for (const auto &socket: std::as_const(m_pending))
        if (socket)
            socket->deleteLater();
    m_pending.clear();
    m_server.close();
    m_discovery.close();
    m_watchdog.stop();
}

QString RemoteGamepadServer::serverName() const
{
    // What the phone lists: a configured name, the static hostname, or a short live hostname.
    // DHCP-assigned names ("host.isp.example") mean nothing to the user.
    const auto configured = QSettings().value("remoteGamepad/name").toString().trimmed();
    if (!configured.isEmpty())
        return configured.left(48);
    QFile file("/etc/hostname");
    if (file.open(QIODevice::ReadOnly)) {
        const auto name = QString::fromUtf8(file.readAll()).trimmed().section('.', 0, 0);
        if (!name.isEmpty() && name != "localhost")
            return name;
    }
    const auto host = QSysInfo::machineHostName();
    if (!host.isEmpty() && host != "localhost" && !host.contains('.'))
        return host;
    const auto user = qEnvironmentVariable("USER");
    return user.isEmpty() ? QStringLiteral("WEMU") : QStringLiteral("WEMU · %1").arg(user);
}

QStringList RemoteGamepadServer::addresses() const
{
    QStringList result;
    for (const auto &interface: QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        const auto name = interface.name();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) || (flags & QNetworkInterface::IsLoopBack) ||
            name.startsWith("docker") || name.startsWith("br-") || name.startsWith("virbr") || name.startsWith("veth"))
            continue;
        for (const auto &entry: interface.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                result << entry.ip().toString();
    }
    return result;
}

QVariantList RemoteGamepadServer::pairedDevices() const
{
    QVariantList result;
    QSettings settings;
    settings.beginGroup("remoteGamepad/devices");
    for (const auto &key: settings.childGroups()) {
        settings.beginGroup(key);
        result << QVariantMap{{"id", settings.value("id")}, {"name", settings.value("name")}, {"platform", settings.value("platform")}};
        settings.endGroup();
    }
    return result;
}

void RemoteGamepadServer::regeneratePin()
{
    // WEMU_REMOTE_PIN pins the PIN for headless runs and automated tests.
    static const auto fixed = qEnvironmentVariable("WEMU_REMOTE_PIN");
    m_pin = fixed.size() == 4 ? fixed : QString::number(QRandomGenerator::system()->bounded(10000)).rightJustified(4, '0');
    emit stateChanged();
}

void RemoteGamepadServer::forgetDevice(const QString &deviceId)
{
    QSettings().remove(settingsKey(deviceId));
    if (deviceId == m_clientId)
        dropClient("forgotten");
    emit pairedDevicesChanged();
}

void RemoteGamepadServer::disconnectClient() { dropClient("disconnected"); }

QString RemoteGamepadServer::gameId(const QString &rpxPath)
{
    return QCryptographicHash::hash(rpxPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
}

QJsonObject RemoteGamepadServer::infoObject() const
{
    return {{"service", "wemu"},
            {"name", serverName()},
            {"protocol", int(RemoteProtocol::version)},
            {"port", port()},
            {"version", QStringLiteral("0.1")}};
}

void RemoteGamepadServer::readDiscovery()
{
    while (m_discovery.hasPendingDatagrams()) {
        const auto datagram = m_discovery.receiveDatagram(512);
        if (!datagram.data().startsWith(RemoteProtocol::discoveryProbe) || !m_server.isListening())
            continue;
        m_discovery.writeDatagram(datagram.makeReply(QJsonDocument(infoObject()).toJson(QJsonDocument::Compact)));
    }
}

void RemoteGamepadServer::acceptClients()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        socket->setParent(this);
        m_pending << socket;
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            if (socket == m_client)
                return readClient();
            // Unauthenticated: only a Hello is accepted, and it must be small.
            *buffer += socket->readAll();
            try {
                Type type{};
                QByteArray payload;
                if (!RemoteProtocol::take(*buffer, type, payload, 4096))
                    return;
                if (type != Type::Hello)
                    throw std::runtime_error("expected hello");
                m_pending.removeAll(socket);
                if (accept(socket, QJsonDocument::fromJson(payload).object())) {
                    m_clientBuffer = std::exchange(*buffer, {});
                    if (!m_clientBuffer.isEmpty())
                        readClient();
                }
            } catch (const std::exception &) {
                socket->abort();
            }
        });
        // Scanners only want this: who is here, and is it WEMU. Idle sockets are not kept.
        sendJson(socket, Type::Info, infoObject());
        QTimer::singleShot(10000, socket, [this, socket] {
            if (socket != m_client)
                socket->abort();
        });
    }
}

bool RemoteGamepadServer::accept(QTcpSocket *socket, const QJsonObject &hello)
{
    const auto reject = [&](const char *reason) {
        sendJson(socket, Type::Reject, {{"reason", reason}});
        socket->disconnectFromHost();
        return false;
    };
    const auto deviceId = hello.value("deviceId").toString().left(128);
    const auto deviceName = hello.value("deviceName").toString().left(64);
    if (hello.value("protocol").toInt() != int(RemoteProtocol::version) || deviceId.isEmpty())
        return reject("protocol");

    QSettings settings;
    const auto key = settingsKey(deviceId);
    const auto token = hello.value("token").toString();
    const bool knownToken = !token.isEmpty() && settings.value(key + "/tokenHash").toString() == tokenHash(token);
    QString issuedToken;
    if (!knownToken) {
        const auto pin = hello.value("pin").toString();
        if (pin.isEmpty())
            return reject("pin_required");
        if (m_lockout.isValid() && m_lockout.elapsed() < lockoutMs)
            return reject("locked");
        if (pin != m_pin) {
            if (++m_failedPins >= maximumFailedPins) {
                m_failedPins = 0;
                m_lockout.start();
                regeneratePin();
            }
            return reject("bad_pin");
        }
        m_failedPins = 0;
        issuedToken = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
        settings.setValue(key + "/id", deviceId);
        settings.setValue(key + "/tokenHash", tokenHash(issuedToken));
        regeneratePin(); // a PIN pairs one phone
    }
    const auto name = deviceName.isEmpty() ? QStringLiteral("Phone") : deviceName;
    settings.setValue(key + "/name", name);
    settings.setValue(key + "/platform", hello.value("platform").toString().left(16));

    // A paired phone reconnecting (or another paired phone) takes the session over.
    dropClient("replaced");
    m_client = socket;
    m_authenticated = true;
    m_clientId = deviceId;
    m_clientName = name;
    m_lastClientMessage.start();
    m_wantStream = false;
    m_source = 0;
    m_sentSequence = m_ackedSequence = 0;
    m_latest = {};
    m_latestFresh = m_encoding = false;
    ++m_generation;
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (socket == m_client)
            dropClient();
    });
    QJsonObject welcome{{"name", serverName()}};
    if (!issuedToken.isEmpty())
        welcome["token"] = issuedToken;
    sendJson(socket, Type::Welcome, welcome);
    sendLibrary();
    sendSession(true);
    qInfo("[REMOTE] GamePad connected: %s", qPrintable(m_clientName));
    emit clientChanged();
    emit pairedDevicesChanged();
    return true;
}

void RemoteGamepadServer::dropClient(const QString &reason)
{
    if (!m_client)
        return;
    auto *socket = m_client.data();
    const bool wasAuthenticated = m_authenticated;
    m_client = nullptr;
    m_authenticated = false;
    m_wantStream = false;
    m_clientBuffer.clear();
    ++m_generation;
    socket->disconnect(this);
    if (!reason.isEmpty() && wasAuthenticated)
        sendJson(socket, Type::Notice, {{"message", reason}});
    socket->disconnectFromHost();
    socket->deleteLater();
    releaseInput();
    updateStreaming();
    if (wasAuthenticated)
        qInfo("[REMOTE] GamePad disconnected (%s)", qPrintable(reason.isEmpty() ? QStringLiteral("closed") : reason));
    m_clientName.clear();
    m_clientId.clear();
    emit clientChanged();
}

void RemoteGamepadServer::releaseInput()
{
    m_launcher.setRemoteButtons(0);
    m_launcher.setRemoteGamepad({});
}

void RemoteGamepadServer::readClient()
{
    if (!m_client)
        return;
    m_clientBuffer += m_client->readAll();
    try {
        Type type{};
        QByteArray payload;
        while (m_client && RemoteProtocol::take(m_clientBuffer, type, payload)) {
            m_lastClientMessage.start();
            handle(type, payload);
        }
    } catch (const std::exception &error) {
        dropClient(QString::fromUtf8(error.what()));
    }
}

void RemoteGamepadServer::handle(Type type, const QByteArray &payload)
{
    switch (type) {
        case Type::Input:
            if (const auto input = RemoteProtocol::parseInput(payload)) {
                m_launcher.setRemoteButtons(input->buttons);
                m_launcher.setRemoteGamepad(input->pad);
            } else
                throw std::runtime_error("bad input");
            break;
        case Type::FrameAck:
            if (payload.size() == 4) {
                const auto sequence = qFromLittleEndian<quint32>(payload.constData());
                if (sequence > m_ackedSequence && sequence <= m_sentSequence)
                    m_ackedSequence = sequence;
                pumpFrames();
            }
            break;
        case Type::Command:
            handleCommand(QJsonDocument::fromJson(payload).object());
            break;
        case Type::Ping:
            send(m_client, Type::Pong, payload.left(8));
            break;
        case Type::Hello:
        default:
            throw std::runtime_error("unexpected message");
    }
}

void RemoteGamepadServer::handleCommand(const QJsonObject &command)
{
    const auto name = command.value("cmd").toString();
    if (name == "library")
        sendLibrary();
    else if (name == "icon")
        sendIcon(command.value("id").toString());
    else if (name == "launch") {
        const auto id = command.value("id").toString();
        if (m_launcher.running()) {
            sendJson(m_client, Type::Notice, {{"message", "already_running"}});
            return;
        }
        for (int row = 0; row < m_library.rowCount({}); ++row) {
            const auto index = m_library.index(row);
            const auto rpx = m_library.data(index, TitleScanner::RpxPathRole).toString();
            if (gameId(rpx) == id) {
                qInfo("[REMOTE] launch requested from %s", qPrintable(m_clientName));
                m_launcher.launch(rpx, m_library.data(index, TitleScanner::NameRole).toString(),
                                  m_library.data(index, TitleScanner::ContentPathRole).toString());
                return;
            }
        }
        sendJson(m_client, Type::Notice, {{"message", "unknown_game"}});
    } else if (name == "stop")
        m_launcher.stop();
    else if (name == "pause") {
        if (!m_launcher.paused())
            m_launcher.togglePause();
    } else if (name == "resume")
        m_launcher.resume();
    else if (name == "stream") {
        m_wantStream = command.value("on").toBool();
        m_source = command.value("source").toString() == "tv" ? 1 : 0;
        m_quality = std::clamp(command.value("quality").toInt(m_quality), 30, 95);
        m_latest = {};
        m_latestFresh = false;
        updateStreaming();
        emit clientChanged();
    }
}

void RemoteGamepadServer::send(QTcpSocket *socket, Type type, const QByteArray &payload)
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
        socket->write(RemoteProtocol::message(type, payload));
}

void RemoteGamepadServer::sendJson(QTcpSocket *socket, Type type, const QJsonObject &object)
{
    send(socket, type, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void RemoteGamepadServer::sendLibrary()
{
    if (!clientConnected())
        return;
    QJsonArray games;
    for (int row = 0; row < m_library.rowCount({}); ++row) {
        const auto index = m_library.index(row);
        const auto titleId = m_library.data(index, TitleScanner::TitleIdRole).toString();
        games.append(QJsonObject{{"id", gameId(m_library.data(index, TitleScanner::RpxPathRole).toString())},
                                 {"name", m_library.data(index, TitleScanner::NameRole).toString()},
                                 {"publisher", m_library.data(index, TitleScanner::PublisherRole).toString()},
                                 {"version", m_library.data(index, TitleScanner::VersionRole).toString()},
                                 // Homebrew ids are "rpx:<host path>": not an id, and not for the phone.
                                 {"titleId", titleId.startsWith("rpx:") ? QString() : titleId},
                                 {"icon", !m_library.data(index, TitleScanner::IconPathRole).toString().isEmpty()}});
    }
    sendJson(m_client, Type::Library, {{"games", games}});
}

void RemoteGamepadServer::sendIcon(const QString &id)
{
    for (int row = 0; row < m_library.rowCount({}); ++row) {
        const auto index = m_library.index(row);
        if (gameId(m_library.data(index, TitleScanner::RpxPathRole).toString()) != id)
            continue;
        const auto url = QUrl(m_library.data(index, TitleScanner::IconPathRole).toString());
        QImage icon(url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (icon.isNull())
            return;
        if (icon.width() > 256 || icon.height() > 256)
            icon = icon.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        icon.save(&buffer, "PNG");
        const auto idBytes = id.toUtf8();
        QByteArray payload(2, '\0');
        qToLittleEndian(quint16(idBytes.size()), payload.data());
        send(m_client, Type::Icon, payload + idBytes + png);
        return;
    }
}

void RemoteGamepadServer::sendSession(bool force)
{
    if (!clientConnected())
        return;
    QString state = "idle";
    if (m_launcher.running())
        state = m_launcher.stopping()                                 ? "stopping"
                : m_launcher.paused()                                 ? "paused"
                : m_launcher.hasFrame()                               ? "running"
                                                                      : "starting";
    const QJsonObject session{{"state", state},
                              {"id", m_launcher.running() ? gameId(m_launcher.gameRpxPath()) : QString()},
                              {"title", m_launcher.running() ? m_launcher.gameTitle() : QString()},
                              {"error", m_launcher.error()}};
    // sessionChanged also fires for every presented frame: only real transitions go out.
    if (session == m_lastSession && !force)
        return;
    m_lastSession = session;
    sendJson(m_client, Type::Session, session);
}

void RemoteGamepadServer::updateStreaming() { m_launcher.setDrcStreaming(streaming() && m_source == 0 && m_launcher.running()); }

void RemoteGamepadServer::offerFrame(const QImage &image, quint8 source)
{
    if (!streaming() || image.isNull())
        return;
    m_latest = image;
    m_latestSource = source;
    m_latestFresh = true;
    pumpFrames();
}

void RemoteGamepadServer::pumpFrames()
{
    if (!streaming() || m_encoding || !m_latestFresh || m_sentSequence - m_ackedSequence >= 2 || m_client->bytesToWrite() > maximumQueuedBytes)
        return;
    m_encoding = true;
    m_latestFresh = false;
    const auto generation = m_generation;
    const auto source = m_latestSource;
    const int quality = m_quality;
    QPointer<RemoteGamepadServer> self(this);
    QThreadPool::globalInstance()->start([self, image = m_latest, generation, source, quality] {
        // The phone shows a 854x480 panel; a TV image is scaled down to match.
        QImage frame = image.width() > 854 ? image.scaled(854, 480, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : image;
        frame = frame.convertToFormat(QImage::Format_RGB888);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        frame.save(&buffer, "JPG", quality);
        const auto width = quint16(frame.width()), height = quint16(frame.height());
        QMetaObject::invokeMethod(
                QCoreApplication::instance(), [self, jpeg = std::move(jpeg), generation, source, width, height] {
                    if (!self)
                        return;
                    self->m_encoding = false;
                    if (generation != self->m_generation || !self->streaming())
                        return;
                    QByteArray payload(9, '\0');
                    qToLittleEndian(++self->m_sentSequence, payload.data());
                    qToLittleEndian(width, payload.data() + 4);
                    qToLittleEndian(height, payload.data() + 6);
                    payload[8] = char(source);
                    self->send(self->m_client, Type::Frame, payload + jpeg);
                    self->pumpFrames();
                },
                Qt::QueuedConnection);
    });
}
