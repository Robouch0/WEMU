#include "PreviewRecorder.hpp"

#include <QFile>

namespace {
    // A boot, then the clip: a game that shows nothing after this long gets no preview.
    constexpr int sessionLimitMs = 120000;
    constexpr int encoderLimitMs = 15000;
    // A game that stops early keeps its clip when it showed at least this much.
    constexpr int shortestClip = 2 * PreviewFrames::outputFps;
    constexpr quint32 buttonA = 0x8000;
    // A game still blank after this many frames (about 8 seconds) may wait for a button: tap A
    // every 2 seconds. Once it shows something, tap A at these clip frames to move past a title screen.
    constexpr quint64 waitingFrames = 480, waitingTapEvery = 120;
    constexpr int clipTaps[]{PreviewFrames::outputFps * 3 / 2, PreviewFrames::outputFps * 3};
} // namespace

PreviewRecorder::PreviewRecorder(QObject *parent) : QObject(parent)
{
    m_release.setSingleShot(true);
    m_release.setInterval(150);
    connect(&m_release, &QTimer::timeout, this, [this] { m_session.setScriptedInput(0, {}); });
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (m_stage == Stage::Finishing) {
            qWarning("[PREVIEW] the encoder did not finish");
            m_encoder.kill();
        } else if (m_stage == Stage::Recording && m_timeline.written() >= shortestClip) {
            finish();
        } else {
            qWarning("[PREVIEW] %s showed nothing in time (%llu frames, all blank)", qUtf8Printable(m_job.name),
                     static_cast<unsigned long long>(m_frames));
            fail();
        }
    });

    connect(&m_session, &EmulatorLauncher::frameReady, this, &PreviewRecorder::onFrame);
    connect(&m_session, &EmulatorLauncher::drcFrameReady, this, [this](const QImage &image) { m_gamepadFrame = image; });
    connect(&m_session, &EmulatorLauncher::runningChanged, this, [this] {
        if (m_session.running())
            return;
        if (m_stage == Stage::Booting || m_stage == Stage::Recording) {
            qWarning("[PREVIEW] %s stopped while recording", qUtf8Printable(m_job.name));
            if (m_timeline.written() >= shortestClip)
                finish();
            else
                fail();
        }
        closeIfStopped();
    });
    connect(&m_encoder, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_stage == Stage::Finishing) {
            QFile::remove(m_job.clipPath);
            if (status == QProcess::NormalExit && code == 0 && QFile::rename(m_job.partPath, m_job.clipPath)) {
                qInfo("[PREVIEW] %s ready", qUtf8Printable(m_job.name));
                close(Ready);
            } else {
                qWarning("[PREVIEW] the encoder failed for %s (exit %d)", qUtf8Printable(m_job.name), code);
                close(Failed);
            }
        } else if (m_stage == Stage::Booting || m_stage == Stage::Recording) {
            qWarning("[PREVIEW] the encoder stopped early for %s (exit %d)", qUtf8Printable(m_job.name), code);
            fail();
        }
        closeIfStopped();
    });
}

PreviewRecorder::~PreviewRecorder()
{
    if (m_stage == Stage::Idle)
        return;
    m_stage = Stage::Idle;
    m_encoder.kill();
    m_encoder.waitForFinished(1000);
    QFile::remove(m_job.partPath);
}

void PreviewRecorder::record(const Job &job)
{
    if (m_stage != Stage::Idle)
        return;
    m_job = job;
    QFile::remove(m_job.partPath);
    QStringList arguments{"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                          "-s", QString("%1x%2").arg(PreviewFrames::clipSize.width()).arg(PreviewFrames::clipSize.height()),
                          "-r", QString::number(PreviewFrames::outputFps), "-i", "-", "-an", "-c:v", m_job.codec};
    if (m_job.codec == "libx264")
        arguments << "-preset" << "veryfast" << "-crf" << "26";
    else
        arguments << "-q:v" << "4";
    arguments << "-pix_fmt" << "yuv420p" << "-movflags" << "+faststart" << "-f" << "mp4" << m_job.partPath;
    m_encoder.setStandardOutputFile(QProcess::nullDevice());
    m_encoder.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    EmulatorLauncher::lowerPriority(m_encoder);
    m_encoder.start(m_job.ffmpeg, arguments);
    m_timeline = {};
    m_frames = 0;
    m_gamepadFrame = {};
    m_fromGamepad = false;
    m_session.setDrcStreaming(false);
    m_stage = Stage::Booting;
    m_deadline.start(sessionLimitMs);
    qInfo("[PREVIEW] recording %s", qUtf8Printable(m_job.name));
    m_session.launch(m_job.rpxPath, m_job.name, m_job.contentPath);
    if (!m_session.running()) {
        qWarning("[PREVIEW] cannot start %s: %s", qUtf8Printable(m_job.name), qUtf8Printable(m_session.error()));
        fail();
    }
}

void PreviewRecorder::abandon()
{
    if (m_stage != Stage::Idle && m_stage != Stage::Closing)
        close(Abandoned);
}

void PreviewRecorder::onFrame(const QImage &image, quint64 sequence)
{
    if (!sequence)
        return;
    ++m_frames;
    if (m_stage == Stage::Booting) {
        if (!PreviewFrames::isFlat(image)) {
            m_stage = Stage::Recording;
        } else if (!PreviewFrames::isFlat(m_gamepadFrame)) {
            qInfo("[PREVIEW] recording the GamePad screen: the TV stays blank");
            m_fromGamepad = true;
            m_stage = Stage::Recording;
        } else if (m_frames >= waitingFrames && (m_frames - waitingFrames) % waitingTapEvery == 0) {
            // Some games draw on the GamePad alone, others wait for a button.
            m_session.setDrcStreaming(true);
            pressA();
        }
    }
    if (m_stage == Stage::Recording) {
        const int before = m_timeline.written();
        if (const int count = m_timeline.take(m_session.frameSwapInterval()); count > 0) {
            for (const int tap: clipTaps)
                if (before < tap && m_timeline.written() >= tap)
                    pressA();
            if (m_encoder.state() == QProcess::Starting)
                m_encoder.waitForStarted(2000);
            const auto frame = PreviewFrames::clipFrame(m_fromGamepad ? m_gamepadFrame : image);
            for (int i = 0; i < count; ++i)
                m_encoder.write(reinterpret_cast<const char *>(frame.constBits()), frame.sizeInBytes());
        }
        if (m_timeline.done())
            finish();
    }
    m_session.acknowledgeFrame(sequence);
}

void PreviewRecorder::pressA()
{
    m_session.setScriptedInput(buttonA, {});
    m_release.start();
}

void PreviewRecorder::finish()
{
    m_stage = Stage::Finishing;
    m_deadline.start(encoderLimitMs);
    m_encoder.closeWriteChannel();
    m_session.stop();
}

void PreviewRecorder::fail() { close(Failed); }

// Both processes stop on their own time; finished is sent once neither runs.
void PreviewRecorder::close(Result result)
{
    m_result = result;
    m_stage = Stage::Closing;
    m_deadline.stop();
    m_release.stop();
    if (m_encoder.state() != QProcess::NotRunning)
        m_encoder.kill();
    m_session.stop();
    closeIfStopped();
}

void PreviewRecorder::closeIfStopped()
{
    if (m_stage != Stage::Closing || m_session.running() || m_encoder.state() != QProcess::NotRunning)
        return;
    if (m_result != Ready)
        QFile::remove(m_job.partPath);
    m_stage = Stage::Idle;
    emit finished(m_job.rpxPath, m_result);
}
