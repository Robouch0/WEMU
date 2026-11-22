#include "GamePreviews.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "AppPreferences.hpp"
#include "EmulatorLauncher.hpp"
#include "PreviewRecorder.hpp"
#include "TitleScanner.hpp"

namespace {
    QString previewKey(const QString &titleId, const QString &rpxPath)
    {
        if (!titleId.isEmpty() && !titleId.startsWith("rpx:"))
            return titleId;
        return "rpx-" + QCryptographicHash::hash(rpxPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    }

    // The folder holding code/, content/ and meta/, or the one holding a lone program.
    QString gameRoot(const QString &rpxPath)
    {
        auto dir = QFileInfo(rpxPath).absoluteDir();
        if (dir.dirName() == "code")
            dir.cdUp();
        return dir.absolutePath();
    }
} // namespace

GamePreviews::GamePreviews(TitleScanner &library, EmulatorLauncher &game, AppPreferences &preferences, QObject *parent) :
    QObject(parent), m_library(library), m_game(game), m_preferences(preferences)
{
    m_cache.setPath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/previews");
    m_next.setSingleShot(true);
    connect(&m_next, &QTimer::timeout, this, &GamePreviews::startNext);

    // Every game frame passes through the recorder: it must not share the menus' thread.
    // Its processes, sockets and timers belong to the thread they are created on.
    m_thread.setObjectName("WEMU previews");
    m_thread.start(QThread::LowPriority);
    auto *anchor = new QObject;
    anchor->moveToThread(&m_thread);
    QMetaObject::invokeMethod(anchor, [this] { m_recorder = new PreviewRecorder; }, Qt::BlockingQueuedConnection);
    anchor->deleteLater();
    connect(&m_thread, &QThread::finished, m_recorder, &QObject::deleteLater);
    connect(m_recorder, &PreviewRecorder::finished, this, &GamePreviews::recorded);

    connect(&m_library, &QAbstractItemModel::modelReset, this, &GamePreviews::rebuild, Qt::QueuedConnection);
    connect(&m_library, &QAbstractItemModel::rowsInserted, this, &GamePreviews::rebuild, Qt::QueuedConnection);
    connect(&m_library, &QAbstractItemModel::rowsRemoved, this, &GamePreviews::rebuild, Qt::QueuedConnection);
    connect(&m_preferences, &AppPreferences::changed, this, [this] {
        if (!enabled())
            abandon();
        touch();
        schedule(1000);
    });
    // Playing comes first: recording gives way, and comes back once the game is closed.
    connect(&m_game, &EmulatorLauncher::stateChanged, this, [this](bool running) {
        if (running)
            abandon();
        else
            schedule(3000);
    });

    m_ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (m_ffmpeg.isEmpty() || !m_cache.mkpath(".")) {
        qInfo("[PREVIEW] FFmpeg not found: no game previews are recorded");
    } else {
        // Prefer H.264; FFmpeg builds without x264 still have the plain MPEG-4 encoder.
        auto *probe = new QProcess(this);
        connect(probe, &QProcess::finished, this, [this, probe] {
            const auto encoders = probe->readAllStandardOutput();
            if (encoders.contains(" libx264 "))
                m_codec = "libx264";
            else if (encoders.contains(" mpeg4 "))
                m_codec = "mpeg4";
            probe->deleteLater();
            touch();
            schedule(1500);
        });
        probe->start(m_ffmpeg, {"-hide_banner", "-encoders"});
    }
    rebuild();
}

GamePreviews::~GamePreviews()
{
    // The recorder stops its core and encoder as it is deleted with the thread.
    disconnect(m_recorder, nullptr, this, nullptr);
    m_thread.quit();
    m_thread.wait();
}

bool GamePreviews::enabled() const { return available() && m_preferences.gamePreviews(); }

void GamePreviews::touch()
{
    ++m_revision;
    emit changed();
}

QUrl GamePreviews::previewUrl(const QString &rpxPath) const
{
    const auto game = m_games.find(rpxPath);
    if (!m_preferences.gamePreviews() || game == m_games.end())
        return {};
    if (QFileInfo(game->ownPreview).isFile())
        return QUrl::fromLocalFile(game->ownPreview);
    if (QFileInfo(clipPath(*game)).isFile())
        return QUrl::fromLocalFile(clipPath(*game));
    return {};
}

bool GamePreviews::pending(const QString &rpxPath) const { return enabled() && (m_current == rpxPath || m_queue.contains(rpxPath)); }

void GamePreviews::rebuild()
{
    m_games.clear();
    m_queue.clear();
    for (int row = 0; row < m_library.rowCount({}); ++row) {
        const auto index = m_library.index(row);
        Game game;
        game.rpxPath = m_library.data(index, TitleScanner::RpxPathRole).toString();
        game.contentPath = m_library.data(index, TitleScanner::ContentPathRole).toString();
        game.name = m_library.data(index, TitleScanner::NameRole).toString();
        game.key = previewKey(m_library.data(index, TitleScanner::TitleIdRole).toString(), game.rpxPath);
        game.ownPreview = gameRoot(game.rpxPath) + "/preview.mp4";
        m_games.insert(game.rpxPath, game);
        if (game.rpxPath != m_current && !QFileInfo::exists(game.ownPreview) && !QFileInfo::exists(clipPath(game)) &&
            !QFileInfo::exists(failedPath(game)))
            m_queue.append(game.rpxPath);
    }
    touch();
    schedule(1500);
}

void GamePreviews::schedule(int delay)
{
    if (!m_next.isActive() || m_next.remainingTime() < delay)
        m_next.start(delay);
}

void GamePreviews::startNext()
{
    if (!m_current.isEmpty() || !enabled() || m_game.running())
        return;
    while (!m_queue.isEmpty()) {
        const auto found = m_games.find(m_queue.takeFirst());
        if (found == m_games.end())
            continue;
        const PreviewRecorder::Job job{found->rpxPath, found->name, found->contentPath,
                                       m_cache.filePath(found->key + ".part.mp4"), clipPath(*found), m_ffmpeg, m_codec};
        m_current = found->rpxPath;
        QMetaObject::invokeMethod(m_recorder, [recorder = m_recorder, job] { recorder->record(job); });
        touch();
        return;
    }
}

void GamePreviews::abandon()
{
    if (!m_current.isEmpty())
        QMetaObject::invokeMethod(m_recorder, [recorder = m_recorder] { recorder->abandon(); });
}

void GamePreviews::recorded(const QString &rpxPath, int result)
{
    m_current.clear();
    const auto game = m_games.find(rpxPath);
    if (result == PreviewRecorder::Abandoned) {
        if (game != m_games.end())
            m_queue.prepend(rpxPath);
    } else if (result == PreviewRecorder::Failed && game != m_games.end()) {
        // The game keeps its cover art from now on.
        QFile marker(failedPath(*game));
        marker.open(QIODevice::WriteOnly);
    }
    touch();
    schedule(500);
}
