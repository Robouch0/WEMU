#pragma once
#include <QImage>
#include <QPainter>
#include <QSize>
#include <algorithm>
#include <cmath>

// Building blocks of a game preview clip: which frames to keep and how to lay them out.
namespace PreviewFrames {
    inline constexpr int outputFps = 30;
    inline constexpr int clipFrames = 5 * outputFps;
    inline constexpr QSize clipSize{640, 360};
    // The Wii U TV refresh rate, in which a guest's swap interval counts.
    inline constexpr double guestRefresh = 60000.0 / 1001.0;

    // A boot screen of a single color (black, white, a logo fade). Real content has contrast somewhere.
    inline bool isFlat(const QImage &image)
    {
        if (image.isNull())
            return true;
        // Dense enough that a small logo on a dark screen is not missed.
        constexpr int columns = 64, rows = 36;
        int lowest = 255, highest = 0;
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column) {
                const auto pixel = image.pixel((2 * column + 1) * image.width() / (2 * columns), (2 * row + 1) * image.height() / (2 * rows));
                const int luma = qGray(pixel);
                lowest = std::min(lowest, luma);
                highest = std::max(highest, luma);
            }
        return highest - lowest < 16;
    }

    // Converts guest frames, each shown for its own number of vertical blanks, into a clip of
    // constant frame rate: slow emulation still plays at game speed.
    class Timeline {
        public:
            // How many clip frames the next guest frame fills.
            int take(unsigned swapInterval)
            {
                if (done())
                    return 0;
                m_guestSeconds += std::max(swapInterval, 1u) / guestRefresh;
                const int covered = std::min(clipFrames, int(std::lround(m_guestSeconds * outputFps)));
                const int count = covered - m_written;
                m_written = covered;
                return count;
            }
            [[nodiscard]] int written() const { return m_written; }
            [[nodiscard]] bool done() const { return m_written >= clipFrames; }

        private:
            double m_guestSeconds{};
            int m_written{};
    };

    // The frame scaled into the clip, letterboxed when its shape differs. Tightly packed RGB.
    inline QImage clipFrame(const QImage &image)
    {
        QImage frame(clipSize, QImage::Format_RGB888);
        frame.fill(Qt::black);
        const auto scaled = image.scaled(clipSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter painter(&frame);
        painter.drawImage((clipSize.width() - scaled.width()) / 2, (clipSize.height() - scaled.height()) / 2, scaled);
        return frame;
    }
} // namespace PreviewFrames
