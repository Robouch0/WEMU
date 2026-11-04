#include <QImage>
#include <gtest/gtest.h>

#include "PreviewFrames.hpp"

TEST(PreviewFramesTest, BootScreensOfOneColorAreFlat)
{
    QImage black(1280, 720, QImage::Format_RGBX8888);
    black.fill(Qt::black);
    EXPECT_TRUE(PreviewFrames::isFlat(black));
    QImage white(1280, 720, QImage::Format_RGBX8888);
    white.fill(Qt::white);
    EXPECT_TRUE(PreviewFrames::isFlat(white));
    EXPECT_TRUE(PreviewFrames::isFlat({}));
}

TEST(PreviewFramesTest, AnythingWithContrastIsContent)
{
    QImage frame(1280, 720, QImage::Format_RGBX8888);
    frame.fill(Qt::black);
    for (int y = 0; y < 360; ++y)
        for (int x = 0; x < 640; ++x)
            frame.setPixel(x, y, qRgb(240, 200, 40));
    EXPECT_FALSE(PreviewFrames::isFlat(frame));
}

TEST(PreviewFramesTest, ASmallLogoOnABlackScreenIsContent)
{
    QImage frame(1280, 720, QImage::Format_RGBX8888);
    frame.fill(Qt::black);
    for (int y = 340; y < 380; ++y)
        for (int x = 600; x < 680; ++x)
            frame.setPixel(x, y, qRgb(220, 220, 255));
    EXPECT_FALSE(PreviewFrames::isFlat(frame));
}

TEST(PreviewFramesTest, SixtyFramesPerSecondGamesFillFiveSecondsAtGameSpeed)
{
    PreviewFrames::Timeline timeline;
    int guestFrames = 0, written = 0;
    while (!timeline.done()) {
        written += timeline.take(1);
        ++guestFrames;
    }
    EXPECT_EQ(written, PreviewFrames::clipFrames);
    // Five seconds of a 59.94 Hz game, give or take a frame.
    EXPECT_NEAR(guestFrames, 300, 1);
    EXPECT_EQ(timeline.take(1), 0);
}

TEST(PreviewFramesTest, ThirtyFramesPerSecondGamesKeepEveryFrame)
{
    PreviewFrames::Timeline timeline;
    int guestFrames = 0;
    while (!timeline.done()) {
        EXPECT_GE(timeline.take(2), 1);
        ++guestFrames;
    }
    EXPECT_NEAR(guestFrames, 150, 1);
}

TEST(PreviewFramesTest, SlowFramesRepeatInTheClip)
{
    PreviewFrames::Timeline timeline;
    // A frame held for twenty blanks lasts a third of a second: ten clip frames.
    EXPECT_EQ(timeline.take(20), 10);
    EXPECT_EQ(timeline.take(0), 1); // No interval reported counts as one blank.
}

TEST(PreviewFramesTest, ClipFramesAreLetterboxedPackedRgb)
{
    QImage square(400, 400, QImage::Format_RGBX8888);
    square.fill(Qt::white);
    const auto frame = PreviewFrames::clipFrame(square);
    EXPECT_EQ(frame.size(), PreviewFrames::clipSize);
    EXPECT_EQ(frame.format(), QImage::Format_RGB888);
    EXPECT_EQ(frame.bytesPerLine(), frame.width() * 3);
    EXPECT_EQ(frame.pixel(5, 180), qRgb(0, 0, 0));
    EXPECT_EQ(frame.pixel(320, 180), qRgb(255, 255, 255));
}
