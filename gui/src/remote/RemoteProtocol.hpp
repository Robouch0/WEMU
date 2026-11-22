#pragma once

#include <QByteArray>
#include <QtEndian>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "gfx/HostPresenter.hpp"

// Wire protocol between WEMU and the mobile GamePad app (mobile/lib/net/protocol.dart mirrors it).
// TCP, little-endian. Every message: u32 length (type byte + payload), u8 type, payload.
// Control messages carry UTF-8 JSON; video and input are binary. See docs/features/mobile-gamepad.md.
namespace RemoteProtocol {
    constexpr quint32 version = 1;
    constexpr quint16 defaultPort = 47470; // TCP session
    constexpr quint16 discoveryPort = 47471; // UDP: "WEMU_DISCOVER" -> JSON server info
    constexpr const char *discoveryProbe = "WEMU_DISCOVER";
    constexpr quint32 maximumClientMessage = 64 * 1024;

    enum class Type : quint8 {
        // phone -> WEMU
        Hello = 0x01, // JSON {protocol, deviceId, deviceName, platform, token?, pin?}
        Input = 0x02, // InputState, binary (inputSize bytes)
        FrameAck = 0x03, // u32 sequence
        Command = 0x04, // JSON {cmd: library|icon|launch|stop|pause|resume|stream, ...}
        Ping = 0x05, // u64 opaque, echoed by Pong
        // WEMU -> phone
        Info = 0x80, // JSON {service, name, protocol, version}, sent on connect (also answers scans)
        Welcome = 0x81, // JSON {name, token}
        Reject = 0x82, // JSON {reason: pin_required|bad_pin|locked|protocol}
        Library = 0x83, // JSON {games: [{id, name, publisher, version, titleId, icon}]}
        Icon = 0x84, // u16 id length, id (UTF-8), PNG bytes
        Session = 0x85, // JSON {state: idle|starting|running|paused|stopping, id, title, error}
        Frame = 0x86, // u32 sequence, u16 width, u16 height, u8 source (0 GamePad, 1 TV), JPEG bytes
        Pong = 0x87, // u64 echoed
        Notice = 0x88, // JSON {message}
    };

    // Input: u32 buttons (VPAD bits), f32 lx ly rx ry, u8 touched, u8 reserved, u16 x, u16 y,
    // u16 reserved, f32 accel xyz (g), f32 gyro xyz (rev/s) = 52 bytes.
    constexpr qsizetype inputSize = 52;
    struct InputState {
            quint32 buttons{};
            Core::Gfx::GamepadState pad;
    };

    inline QByteArray message(Type type, const QByteArray &payload = {})
    {
        QByteArray out(5, '\0');
        qToLittleEndian(quint32(payload.size() + 1), out.data());
        out[4] = char(type);
        return out + payload;
    }

    // Splits one complete message off the stream buffer; throws on a malformed length.
    inline bool take(QByteArray &buffer, Type &type, QByteArray &payload, quint32 limit = maximumClientMessage)
    {
        if (buffer.size() < 5)
            return false;
        const auto size = qFromLittleEndian<quint32>(buffer.constData());
        if (size < 1 || size > limit)
            throw std::runtime_error("Invalid remote message length");
        if (buffer.size() < qsizetype(size) + 4)
            return false;
        type = Type(quint8(buffer[4]));
        payload = buffer.mid(5, size - 1);
        buffer.remove(0, qsizetype(size) + 4);
        return true;
    }

    inline std::optional<InputState> parseInput(const QByteArray &payload)
    {
        if (payload.size() != inputSize)
            return std::nullopt;
        const auto *p = reinterpret_cast<const uchar *>(payload.constData());
        const auto f32 = [p](int offset) {
            const auto bits = qFromLittleEndian<quint32>(p + offset);
            float value;
            std::memcpy(&value, &bits, sizeof value);
            return value;
        };
        InputState input;
        input.buttons = qFromLittleEndian<quint32>(p);
        input.pad.leftStick = {f32(4), f32(8)};
        input.pad.rightStick = {f32(12), f32(16)};
        input.pad.touched = p[20] != 0;
        input.pad.touchX = qFromLittleEndian<quint16>(p + 22);
        input.pad.touchY = qFromLittleEndian<quint16>(p + 24);
        for (int axis = 0; axis < 3; ++axis) {
            input.pad.accel[axis] = f32(28 + axis * 4);
            input.pad.gyro[axis] = f32(40 + axis * 4);
        }
        return input;
    }
} // namespace RemoteProtocol
