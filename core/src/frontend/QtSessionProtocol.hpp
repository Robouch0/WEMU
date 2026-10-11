#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QtEndian>
#include <stdexcept>

namespace WemuSession {
    constexpr quint32 version = 2;
    constexpr quint32 maximumMessage = 4096u * 4096u * 4u + 128u;
    enum class Type : quint32 { Hello = 1, Frame, PauseState, Command, FrameAck };
    inline void configure(QDataStream &stream)
    {
        stream.setVersion(QDataStream::Qt_6_0);
        stream.setByteOrder(QDataStream::LittleEndian);
    }
    inline QByteArray packetHeader(const QByteArray &body, qsizetype payloadBytes)
    {
        if (body.size() < 4 || payloadBytes < 0 || body.size() > maximumMessage || payloadBytes > maximumMessage - body.size())
            throw std::runtime_error("Invalid session message size");
        QByteArray result(4, '\0');
        qToLittleEndian(quint32(body.size() + payloadBytes), result.data());
        result += body;
        return result;
    }
    inline QByteArray packet(const QByteArray &body) { return packetHeader(body, 0); }
    // A stream can split messages at any byte. Invalid lengths never allocate a frame.
    inline bool take(QByteArray &buffer, QByteArray &body)
    {
        if (buffer.size() < 4)
            return false;
        const auto size = qFromLittleEndian<quint32>(buffer.constData());
        if (size < 4 || size > maximumMessage)
            throw std::runtime_error("Invalid session message length");
        if (buffer.size() < qsizetype(size) + 4)
            return false;
        if (buffer.size() == qsizetype(size) + 4) {
            body.swap(buffer);
            buffer.clear();
            body.remove(0, 4);
        } else {
            body = buffer.mid(4, size);
            buffer.remove(0, qsizetype(size) + 4);
        }
        return true;
    }
} // namespace WemuSession
