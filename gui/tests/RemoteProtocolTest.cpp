#include <QtEndian>
#include <cstring>
#include <gtest/gtest.h>

#include "RemoteProtocol.hpp"

namespace {
    void putFloat(QByteArray &bytes, int offset, float value)
    {
        quint32 bits;
        std::memcpy(&bits, &value, sizeof bits);
        qToLittleEndian(bits, bytes.data() + offset);
    }
} // namespace

TEST(RemoteProtocolTest, MessagesSplitAtAnyByteAndCoalesce)
{
    const auto a = RemoteProtocol::message(RemoteProtocol::Type::Ping, QByteArray(8, '\x07'));
    const auto b = RemoteProtocol::message(RemoteProtocol::Type::Command, R"({"cmd":"stop"})");
    const auto stream = a + b;
    QByteArray buffer, payload;
    RemoteProtocol::Type type{};
    std::vector<std::pair<RemoteProtocol::Type, QByteArray>> received;
    for (const char byte: stream) {
        buffer += byte;
        while (RemoteProtocol::take(buffer, type, payload))
            received.emplace_back(type, payload);
    }
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].first, RemoteProtocol::Type::Ping);
    EXPECT_EQ(received[0].second, QByteArray(8, '\x07'));
    EXPECT_EQ(received[1].second, QByteArray(R"({"cmd":"stop"})"));
    EXPECT_TRUE(buffer.isEmpty());

    QByteArray oversized(5, '\0');
    qToLittleEndian(quint32(RemoteProtocol::maximumClientMessage + 1), oversized.data());
    EXPECT_THROW(RemoteProtocol::take(oversized, type, payload), std::runtime_error);
    QByteArray empty(5, '\0');
    EXPECT_THROW(RemoteProtocol::take(empty, type, payload), std::runtime_error);
}

TEST(RemoteProtocolTest, InputSamplesDecodeEveryField)
{
    QByteArray input(RemoteProtocol::inputSize, '\0');
    qToLittleEndian(quint32(0x8040), input.data());
    putFloat(input, 4, -0.5f);
    putFloat(input, 16, 1.0f);
    input[20] = 1;
    qToLittleEndian(quint16(853), input.data() + 22);
    qToLittleEndian(quint16(12), input.data() + 24);
    putFloat(input, 32, -1.0f);
    putFloat(input, 48, 0.25f);
    const auto parsed = RemoteProtocol::parseInput(input);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->buttons, 0x8040u);
    EXPECT_FLOAT_EQ(parsed->pad.leftStick[0], -0.5f);
    EXPECT_FLOAT_EQ(parsed->pad.rightStick[1], 1.0f);
    EXPECT_TRUE(parsed->pad.touched);
    EXPECT_EQ(parsed->pad.touchX, 853);
    EXPECT_EQ(parsed->pad.touchY, 12);
    EXPECT_FLOAT_EQ(parsed->pad.accel[1], -1.0f);
    EXPECT_FLOAT_EQ(parsed->pad.gyro[2], 0.25f);
    EXPECT_FALSE(RemoteProtocol::parseInput(input.left(51)).has_value());
}
