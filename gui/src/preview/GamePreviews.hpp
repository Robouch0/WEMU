#pragma once
#include <QDir>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>

class AppPreferences;
class EmulatorLauncher;
class PreviewRecorder;
class TitleScanner;

// A short clip of each game for the library. A preview.mp4 beside the game wins; otherwise WEMU
// records one in the background (PreviewRecorder, on its own thread). One game at a time, and
// never while one is being played.
class GamePreviews : public QObject {
        Q_OBJECT
        // Changes whenever a preview appears or recording moves on, for QML bindings to re-read.
        Q_PROPERTY(int revision READ revision NOTIFY changed)
        // FFmpeg, which encodes the clips, was found.
        Q_PROPERTY(bool available READ available NOTIFY changed)

    public:
        GamePreviews(TitleScanner &library, EmulatorLauncher &game, AppPreferences &preferences, QObject *parent = nullptr);
        ~GamePreviews() override;

        [[nodiscard]] int revision() const { return m_revision; }
        [[nodiscard]] bool available() const { return !m_codec.isEmpty(); }
        // The clip to play for this game, or an empty URL.
        Q_INVOKABLE [[nodiscard]] QUrl previewUrl(const QString &rpxPath) const;
        // A clip is still on its way for this game.
        Q_INVOKABLE [[nodiscard]] bool pending(const QString &rpxPath) const;

    signals:
        void changed();

    private:
        struct Game {
                QString rpxPath, contentPath, name, key;
                QString ownPreview; // preview.mp4 next to the game
        };

        void rebuild();
        void schedule(int delay);
        void startNext();
        void abandon();
        void recorded(const QString &rpxPath, int result);
        [[nodiscard]] bool enabled() const;
        [[nodiscard]] QString clipPath(const Game &game) const { return m_cache.filePath(game.key + ".mp4"); }
        [[nodiscard]] QString failedPath(const Game &game) const { return m_cache.filePath(game.key + ".none"); }
        void touch();

        TitleScanner &m_library;
        EmulatorLauncher &m_game;
        AppPreferences &m_preferences;
        QThread m_thread;
        PreviewRecorder *m_recorder{};
        QString m_ffmpeg, m_codec;
        QDir m_cache;
        QHash<QString, Game> m_games; // by program path
        QStringList m_queue;
        QString m_current; // being recorded, until the recorder reports back
        QTimer m_next;
        int m_revision = 0;
};
