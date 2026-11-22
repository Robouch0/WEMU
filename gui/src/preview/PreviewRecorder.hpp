#pragma once
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include "EmulatorLauncher.hpp"
#include "PreviewFrames.hpp"

// Records one game's preview clip: runs it without window or sound and keeps the first seconds
// that show something, tapping A to get past title screens. A game that keeps the TV blank is
// recorded from its GamePad screen. Lives on its own thread: every game frame passes through it.
class PreviewRecorder : public QObject {
        Q_OBJECT

    public:
        enum Result { Ready, Failed, Abandoned };
        struct Job {
                QString rpxPath, name, contentPath;
                QString partPath, clipPath; // encoded into the first, renamed to the second when complete
                QString ffmpeg, codec;
        };

        explicit PreviewRecorder(QObject *parent = nullptr);
        ~PreviewRecorder() override;

        void record(const Job &job);
        // Stops without a verdict, for the game to be recorded another time.
        void abandon();

    signals:
        // The core and the encoder have both stopped. result is a Result.
        void finished(const QString &rpxPath, int result);

    private:
        enum class Stage { Idle, Booting, Recording, Finishing, Closing };

        void onFrame(const QImage &image, quint64 sequence);
        void pressA();
        void finish();
        void fail();
        void close(Result result);
        void closeIfStopped();

        EmulatorLauncher m_session{nullptr, EmulatorLauncher::Mode::Background};
        QProcess m_encoder;
        QTimer m_deadline, m_release;
        Job m_job;
        Stage m_stage = Stage::Idle;
        Result m_result = Failed;
        PreviewFrames::Timeline m_timeline;
        quint64 m_frames = 0;
        QImage m_gamepadFrame;
        bool m_fromGamepad = false;
};
