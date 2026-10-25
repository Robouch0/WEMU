#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QtEndian>
#include <algorithm>
#include <stdexcept>

#include "gfx/HostPresenter.hpp"

namespace WemuSession {
    constexpr quint32 version = 4;
    constexpr quint32 maximumMessage = 4096u * 4096u * 4u + 128u;
    // Frame/DrcFrame: core -> window. Gamepad: window -> core, the analog GamePad state
    // (sticks, touch, motion) of a remote GamePad; its buttons use Command/Buttons.
    enum class Type : quint32 { Hello = 1, Frame, PauseState, Command, FrameAck, DrcFrame, Gamepad };
    constexpr quint32 drcWidth = 854, drcHeight = 480;
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
    inline void writeGamepad(QDataStream &stream, const Core::Gfx::GamepadState &state)
    {
        stream << state.leftStick[0] << state.leftStick[1] << state.rightStick[0] << state.rightStick[1] << quint8(state.touched)
               << quint16(state.touchX) << quint16(state.touchY);
        for (const float value: state.accel)
            stream << value;
        for (const float value: state.gyro)
            stream << value;
    }
    // Out-of-range values are clamped, never trusted: the source is a network peer.
    inline Core::Gfx::GamepadState readGamepad(QDataStream &stream)
    {
        Core::Gfx::GamepadState state;
        quint8 touched{};
        quint16 x{}, y{};
        stream >> state.leftStick[0] >> state.leftStick[1] >> state.rightStick[0] >> state.rightStick[1] >> touched >> x >> y;
        for (float &value: state.accel)
            stream >> value;
        for (float &value: state.gyro)
            stream >> value;
        const auto clamp = [](float value, float limit) { return value == value ? std::clamp(value, -limit, limit) : 0.0f; };
        for (float *axis: {&state.leftStick[0], &state.leftStick[1], &state.rightStick[0], &state.rightStick[1]})
            *axis = clamp(*axis, 1.0f);
        for (float &value: state.accel)
            value = clamp(value, 8.0f);
        for (float &value: state.gyro)
            value = clamp(value, 16.0f);
        state.touched = touched != 0;
        state.touchX = std::min<quint16>(x, drcWidth - 1);
        state.touchY = std::min<quint16>(y, drcHeight - 1);
        return state;
    }
} // namespace WemuSession
