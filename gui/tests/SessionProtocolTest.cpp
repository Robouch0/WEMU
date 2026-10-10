#include <gtest/gtest.h>

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
