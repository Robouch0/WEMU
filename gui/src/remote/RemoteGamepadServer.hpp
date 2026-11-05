#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>

#include "RemoteProtocol.hpp"

class EmulatorLauncher;
class TitleScanner;

// Lets a phone running the WEMU GamePad app act as the Wii U GamePad over Wi-Fi: discovery,
// PIN pairing, library browsing and launching, GamePad-screen streaming (JPEG) and input.
// One phone drives the session at a time; a paired phone reconnecting takes over.
class RemoteGamepadServer : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
        Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
        Q_PROPERTY(QString error READ error NOTIFY stateChanged)
        Q_PROPERTY(QString pin READ pin NOTIFY stateChanged)
        Q_PROPERTY(int port READ port NOTIFY stateChanged)
        Q_PROPERTY(QString serverName READ serverName NOTIFY stateChanged)
        Q_PROPERTY(QStringList addresses READ addresses NOTIFY stateChanged)
        Q_PROPERTY(bool clientConnected READ clientConnected NOTIFY clientChanged)
        Q_PROPERTY(QString clientName READ clientName NOTIFY clientChanged)
        Q_PROPERTY(bool streaming READ streaming NOTIFY clientChanged)
        Q_PROPERTY(QVariantList pairedDevices READ pairedDevices NOTIFY pairedDevicesChanged)
    public:
        RemoteGamepadServer(EmulatorLauncher &launcher, TitleScanner &library, QObject *parent = nullptr);
        ~RemoteGamepadServer() override;

        bool enabled() const { return m_enabled; }
        void setEnabled(bool enabled);
        bool listening() const { return m_server.isListening(); }
        QString error() const { return m_error; }
        QString pin() const { return m_pin; }
        int port() const { return m_server.isListening() ? m_server.serverPort() : RemoteProtocol::defaultPort; }
        QString serverName() const;
        QStringList addresses() const;
        bool clientConnected() const { return m_client && m_authenticated; }
        QString clientName() const { return m_clientName; }
        bool streaming() const { return clientConnected() && m_wantStream; }
        QVariantList pairedDevices() const;

        Q_INVOKABLE void regeneratePin();
        Q_INVOKABLE void disconnectClient();
        Q_INVOKABLE void forgetDevice(const QString &deviceId);

        // Stable per-title id shared with the phone (the RPX path is host-specific).
        static QString gameId(const QString &rpxPath);

    signals:
        void stateChanged();
        void clientChanged();
        void pairedDevicesChanged();

    private:
        void start();
        void stop();
        void acceptClients();
        void readClient();
        void readDiscovery();
        void dropClient(const QString &reason = {});
        void handle(RemoteProtocol::Type type, const QByteArray &payload);
        bool accept(QTcpSocket *socket, const QJsonObject &hello);
        void handleCommand(const QJsonObject &command);
        void send(QTcpSocket *socket, RemoteProtocol::Type type, const QByteArray &payload = {});
        void sendJson(QTcpSocket *socket, RemoteProtocol::Type type, const QJsonObject &object);
        void sendLibrary();
        void sendIcon(const QString &id);
        void sendSession(bool force = false);
        void updateStreaming();
        void offerFrame(const QImage &image, quint8 source);
        void pumpFrames();
        void releaseInput();
        QJsonObject infoObject() const;
        QJsonObject m_lastSession;

        EmulatorLauncher &m_launcher;
        TitleScanner &m_library;
        QTcpServer m_server;
        QUdpSocket m_discovery;
        QTimer m_watchdog;
        bool m_enabled{};
        QString m_error, m_pin;

        // Only m_client is ever authenticated; m_pending sockets wait for their Hello.
        QPointer<QTcpSocket> m_client;
        QList<QPointer<QTcpSocket>> m_pending;
        QByteArray m_clientBuffer;
        bool m_authenticated{};
        QString m_clientName, m_clientId;
        QElapsedTimer m_lastClientMessage;
        int m_failedPins{};
        QElapsedTimer m_lockout;

        // Streaming: newest image wins; at most two frames await acknowledgement.
        bool m_wantStream{};
        quint8 m_source{}; // 0 GamePad, 1 TV
        int m_quality = 75;
        QImage m_latest;
        quint8 m_latestSource{};
        bool m_latestFresh{}, m_encoding{};
        quint32 m_sentSequence{}, m_ackedSequence{};
        quint64 m_generation{}; // invalidates encodes that finish after the client changed
};
