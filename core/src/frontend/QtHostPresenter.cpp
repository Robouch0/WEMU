#include "frontend/QtHostPresenter.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <utility>

#include "frontend/QtSessionProtocol.hpp"

namespace Core::Frontend {
    QtHostPresenter::QtHostPresenter(const QString &serverName)
    {
        m_socket.connectToServer(serverName);
        if (!m_socket.waitForConnected(5000))
            throw std::runtime_error("Unable to connect to the WEMU window");
        QByteArray body;
        QDataStream stream(&body, QIODevice::WriteOnly);
        WemuSession::configure(stream);
        stream << quint32(WemuSession::Type::Hello) << WemuSession::version;
        if (!send(body))
            throw std::runtime_error("WEMU window disconnected during startup");
    }

    bool QtHostPresenter::connected() const { return !m_stopping && m_socket.state() == QLocalSocket::ConnectedState; }

    bool QtHostPresenter::send(const QByteArray &body, std::span<const std::uint8_t> payload)
    {
        const auto bytes = WemuSession::packetHeader(body, qsizetype(payload.size()));
        if (!connected() || m_socket.write(bytes) != bytes.size())
            return false;
        // QLocalSocket copies queued bytes before write returns, so the caller's
        // framebuffer can be reused as soon as this synchronous send completes.
        if (!payload.empty() &&
            m_socket.write(reinterpret_cast<const char *>(payload.data()), qsizetype(payload.size())) != qsizetype(payload.size()))
            return false;
        while (connected() && m_socket.bytesToWrite()) {
            m_socket.flush();
            if (m_socket.bytesToWrite())
                m_socket.waitForBytesWritten(50);
            receive({});
        }
        return connected();
    }

    void QtHostPresenter::receive(std::chrono::milliseconds wait)
    {
        if (wait.count() && !m_socket.bytesAvailable() && connected())
            m_socket.waitForReadyRead(int(wait.count()));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        // Control messages are small, but a stalled guest lets a remote GamePad's input queue up:
        // bound each read and drain parsed messages, so only one oversized message is an error.
        QByteArray body;
        while (true) {
            if (!WemuSession::take(m_incoming, body)) {
                if (m_incoming.size() > 4096)
                    throw std::runtime_error("Oversized session control buffer");
                const auto chunk = m_socket.read(4096);
                if (chunk.isEmpty())
                    break;
                m_incoming += chunk;
                continue;
            }
            QDataStream stream(body);
            WemuSession::configure(stream);
            quint32 type{};
            stream >> type;
            if (type == quint32(WemuSession::Type::FrameAck)) {
                quint64 sequence{};
                stream >> sequence;
                if (sequence != m_acknowledged + 1 || sequence > m_sequence)
                    throw std::runtime_error("Invalid session frame acknowledgement");
                m_acknowledged = sequence;
            } else if (type == quint32(WemuSession::Type::Command)) {
                quint32 action{}, value{};
                stream >> action >> value;
                if (action < quint32(Action::Pause) || action > quint32(Action::AudioVolume))
                    throw std::runtime_error("Invalid session control action");
                m_commands.push_back({Action(action), value});
                if (Action(action) == Action::Stop)
                    m_stopping = true;
            } else if (type == quint32(WemuSession::Type::Gamepad)) {
                m_gamepad = WemuSession::readGamepad(stream);
            } else
                throw std::runtime_error("Unexpected session control message");
            if (stream.status() != QDataStream::Ok || !stream.atEnd())
                throw std::runtime_error("Invalid session control payload");
        }
    }

    std::vector<QtHostPresenter::Command> QtHostPresenter::poll(std::chrono::milliseconds wait)
    {
        receive(wait);
        return std::exchange(m_commands, {});
    }

    bool QtHostPresenter::present(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, const Statistics &statistics)
    {
        if (!width || !height || width > 4096 || height > 4096 || std::uint64_t(width) * height * 4 != rgba.size())
            throw std::runtime_error("Invalid session framebuffer");
        QByteArray body;
        QDataStream stream(&body, QIODevice::WriteOnly);
        WemuSession::configure(stream);
        stream << quint32(WemuSession::Type::Frame) << ++m_sequence << quint32(width) << quint32(height) << statistics.presentFps
               << statistics.movieFps << quint64(statistics.presents) << quint64(statistics.movies);
        if (!send(body, rgba))
            return false;
        // Two-frame pipeline, like the standalone presentation slots. Every visible frame is acknowledged in order.
        while (connected() && m_sequence - m_acknowledged >= 2)
            receive(std::chrono::milliseconds(50));
        return connected();
    }

    bool QtHostPresenter::presentDrc(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height)
    {
        if (!width || !height || width > 4096 || height > 4096 || std::uint64_t(width) * height * 4 != rgba.size())
            throw std::runtime_error("Invalid session GamePad framebuffer");
        // Unacknowledged: the window keeps only the newest GamePad image, so the TV frame pipeline
        // alone paces the guest.
        QByteArray body;
        QDataStream stream(&body, QIODevice::WriteOnly);
        WemuSession::configure(stream);
        stream << quint32(WemuSession::Type::DrcFrame) << quint32(width) << quint32(height);
        return send(body, rgba);
    }

    void QtHostPresenter::paused(bool value, const Statistics &statistics)
    {
        // Settle submitted presentation before acknowledging the CPU pause checkpoint.
        if (value)
            while (connected() && m_acknowledged != m_sequence)
                receive(std::chrono::milliseconds(50));
        QByteArray body;
        QDataStream stream(&body, QIODevice::WriteOnly);
        WemuSession::configure(stream);
        stream << quint32(WemuSession::Type::PauseState) << quint32(value) << quint64(statistics.presents) << quint64(statistics.movies);
        send(body);
    }
} // namespace Core::Frontend
