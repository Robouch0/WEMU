#include <gtest/gtest.h>
#include <limits>

#include "SessionFrame.hpp"
#include "frontend/QtSessionProtocol.hpp"

TEST(SessionProtocolTest, SplitAndCoalescedPacketsPreserveMessageBoundaries)
{
    QByteArray body;
    QDataStream stream(&body, QIODevice::WriteOnly);
    WemuSession::configure(stream);
    stream << quint32(WemuSession::Type::Hello) << WemuSession::version;
    const auto packet = WemuSession::packet(body);
    for (qsizetype split = 0; split < packet.size(); ++split) {
        auto buffer = packet.first(split);
        QByteArray decoded;
        EXPECT_FALSE(WemuSession::take(buffer, decoded));
        buffer += packet.sliced(split);
        buffer += packet;
        EXPECT_TRUE(WemuSession::take(buffer, decoded));
        EXPECT_EQ(decoded, body);
        EXPECT_TRUE(WemuSession::take(buffer, decoded));
        EXPECT_EQ(decoded, body);
        EXPECT_TRUE(buffer.isEmpty());
    }
}

TEST(SessionProtocolTest, InvalidLengthsAreRejectedBeforeAllocatingPayloads)
{
    for (quint32 length: {0u, 3u, WemuSession::maximumMessage + 1, 0xFFFFFFFFu}) {
        QByteArray buffer(4, '\0'), decoded;
        qToLittleEndian(length, buffer.data());
        EXPECT_THROW(WemuSession::take(buffer, decoded), std::runtime_error);
        EXPECT_TRUE(decoded.isEmpty());
    }
    EXPECT_THROW(WemuSession::packet(QByteArray(3, '\0')), std::runtime_error);
}

TEST(SessionProtocolTest, SeparatePixelPayloadUsesIdenticalWireFormat)
{
    QByteArray metadata;
    QDataStream stream(&metadata, QIODevice::WriteOnly);
    WemuSession::configure(stream);
    stream << quint32(WemuSession::Type::Frame) << quint64(17) << quint32(2) << quint32(1) << 30.0 << 15.0 << quint64(18) << quint64(9);
    const QByteArray pixels("\x01\x02\x03\xff\x04\x05\x06\xff", 8);
    const auto bytes = WemuSession::packetHeader(metadata, pixels.size()) + pixels;
    EXPECT_EQ(bytes, WemuSession::packet(metadata + pixels));
    for (qsizetype split = 0; split < bytes.size(); ++split) {
        auto buffer = bytes.first(split);
        QByteArray decoded;
        EXPECT_FALSE(WemuSession::take(buffer, decoded));
        buffer += bytes.sliced(split);
        ASSERT_TRUE(WemuSession::take(buffer, decoded));
        EXPECT_EQ(decoded, metadata + pixels);
        EXPECT_TRUE(buffer.isEmpty());
    }
    EXPECT_THROW(WemuSession::packetHeader(metadata, -1), std::runtime_error);
    EXPECT_THROW(WemuSession::packetHeader(metadata, WemuSession::maximumMessage), std::runtime_error);
}

TEST(SessionProtocolTest, FrameStorageSurvivesPacketReuseAndWritableCopiesDetach)
{
    QByteArray packet(52, '\0');
    const QByteArray pixels("\x01\x02\x03\xff\x04\x05\x06\xff", 8);
    packet += pixels;
    const auto *source = reinterpret_cast<const uchar *>(packet.constData() + 52);
    auto image = WemuSession::frameImage(std::move(packet), 52, 2, 1);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.constBits(), source); // No pixel allocation or copy.
    auto renderThreadImage = image;
    image = {};
    packet = QByteArray(1024, 'x');
    EXPECT_EQ(QByteArray(reinterpret_cast<const char *>(renderThreadImage.constBits()), 8), pixels);
    auto writable = renderThreadImage;
    writable.bits()[0] = 42;
    EXPECT_EQ(renderThreadImage.constBits()[0], 1);
    EXPECT_NE(writable.constBits(), source);
    renderThreadImage = {};
    EXPECT_EQ(writable.constBits()[0], 42);
    EXPECT_THROW(WemuSession::frameImage(QByteArray(8, '\0'), 0, 0, 1), std::runtime_error);
    EXPECT_THROW(WemuSession::frameImage(QByteArray(8, '\0'), -1, 2, 1), std::runtime_error);
    EXPECT_THROW(WemuSession::frameImage(QByteArray(8, '\0'), 9, 2, 1), std::runtime_error);
    EXPECT_THROW(WemuSession::frameImage(QByteArray(7, '\0'), 0, 2, 1), std::runtime_error);
}

TEST(SessionProtocolTest, ScanoutPaddingIsOpaqueEvenWhenTheGuestClearsItToZero)
{
    const QByteArray packet("\x11\x22\x33\x00\x44\x55\x66\x40", 8);
    auto image = WemuSession::frameImage(packet, 0, 2, 1);
    EXPECT_EQ(image.constBits()[0], 0x11);
    EXPECT_EQ(image.constBits()[3], 255);
    EXPECT_EQ(image.constBits()[7], 255);
    EXPECT_EQ(static_cast<unsigned char>(packet[3]), 0); // shared wire packet stays immutable
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 255);
}

TEST(SessionProtocolTest, GamepadStateRoundTripsAndClampsUntrustedValues)
{
    Core::Gfx::GamepadState state;
    state.leftStick = {0.5f, -0.25f};
    state.rightStick = {3.0f, std::numeric_limits<float>::quiet_NaN()};
    state.touched = true;
    state.touchX = 900;
    state.touchY = 120;
    state.accel = {0.0f, -1.0f, 20.0f};
    state.gyro = {0.25f, 0.0f, -0.5f};
    QByteArray body;
    {
        QDataStream out(&body, QIODevice::WriteOnly);
        WemuSession::configure(out);
        WemuSession::writeGamepad(out, state);
    }
    QDataStream in(body);
    WemuSession::configure(in);
    const auto read = WemuSession::readGamepad(in);
    EXPECT_EQ(in.status(), QDataStream::Ok);
    EXPECT_TRUE(in.atEnd());
    EXPECT_FLOAT_EQ(read.leftStick[0], 0.5f);
    EXPECT_FLOAT_EQ(read.leftStick[1], -0.25f);
    EXPECT_FLOAT_EQ(read.rightStick[0], 1.0f);
    EXPECT_FLOAT_EQ(read.rightStick[1], 0.0f);
    EXPECT_TRUE(read.touched);
    EXPECT_EQ(read.touchX, 853);
    EXPECT_EQ(read.touchY, 120);
    EXPECT_FLOAT_EQ(read.accel[2], 8.0f);
    EXPECT_FLOAT_EQ(read.gyro[2], -0.5f);
}
