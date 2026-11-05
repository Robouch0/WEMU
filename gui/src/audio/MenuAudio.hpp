#pragma once
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <SDL2/SDL_audio.h>
#include <memory>

#include "MenuMixer.hpp"
#include "SpectrumAnalyzer.hpp"

// Plays WEMU's menu sounds on its own audio device. Games have their own output in the core.
class MenuAudio : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY settingsChanged)
    Q_PROPERTY(bool musicEnabled READ musicEnabled WRITE setMusicEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool testPlaying READ testPlaying NOTIFY testChanged)
    Q_PROPERTY(double testProgress READ testProgress NOTIFY testChanged)
    Q_PROPERTY(bool spectrumActive READ spectrumActive WRITE setSpectrumActive NOTIFY spectrumActiveChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)

public:
    explicit MenuAudio(QObject *parent = nullptr);
    ~MenuAudio() override;

    [[nodiscard]] bool available() const { return m_device != 0; }
    [[nodiscard]] int volume() const;
    void setVolume(int volume);
    [[nodiscard]] bool musicEnabled() const;
    void setMusicEnabled(bool enabled);
    [[nodiscard]] bool testPlaying() const { return m_testPlaying; }
    [[nodiscard]] double testProgress() const { return m_testProgress; }
    [[nodiscard]] bool spectrumActive() const { return m_spectrumActive; }
    void setSpectrumActive(bool active);
    [[nodiscard]] QVariantList spectrum() const { return m_spectrum; }

    Q_INVOKABLE void play(const QString &effect);
    Q_INVOKABLE void playTest(const QString &name);
    Q_INVOKABLE void stopTest();

public slots:
    void setGameRunning(bool running);

signals:
    void settingsChanged();
    void testChanged();
    void spectrumActiveChanged();
    void spectrumChanged();

private:
    static void callback(void *self, Uint8 *stream, int bytes);
    [[nodiscard]] std::shared_ptr<const MenuMixer::Clip> load(const QString &name) const;
    void refreshDisplay();
    void updateTimer();

    SDL_AudioDeviceID m_device = 0;
    MenuMixer m_mixer;
    SpectrumAnalyzer m_analyzer;
    QHash<QString, std::shared_ptr<const MenuMixer::Clip>> m_clips;
    QTimer m_display;
    QTimer m_pauseAfterFade;
    QVariantList m_spectrum;
    std::vector<float> m_levels;
    qint64 m_lastEffect = 0;
    qint64 m_quietUntil = 0;
    bool m_gameRunning = false;
    bool m_testPlaying = false;
    double m_testProgress = 0;
    bool m_spectrumActive = false;
};
