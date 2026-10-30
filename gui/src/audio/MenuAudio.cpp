#include "MenuAudio.hpp"
#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr int bandCount = 32;
constexpr int displayIntervalMs = 16;
constexpr int startupQuietMs = 600;     // Building the first page moves focus several times.
constexpr int moveSpacingMs = 70;       // Holding a direction should not rattle.
constexpr int gamePauseDelayMs = 1500;  // Lets the launch sound and the music fade finish.
}

MenuAudio::MenuAudio(QObject *parent)
    : QObject(parent), m_analyzer(MenuMixer::historySize, MenuMixer::sampleRate, bandCount), m_levels(bandCount, 0.f)
{
    m_quietUntil = QDateTime::currentMSecsSinceEpoch() + startupQuietMs;
    for (int band = 0; band < bandCount; ++band) m_spectrum.append(0.0);

    connect(&m_display, &QTimer::timeout, this, &MenuAudio::refreshDisplay);
    m_display.setInterval(displayIntervalMs);
    m_pauseAfterFade.setSingleShot(true);
    m_pauseAfterFade.setInterval(gamePauseDelayMs);
    connect(&m_pauseAfterFade, &QTimer::timeout, this, [this] { SDL_PauseAudioDevice(m_device, 1); });

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        qWarning("[AUDIO] Menu audio is unavailable: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec wanted{};
    wanted.freq = MenuMixer::sampleRate;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    wanted.callback = &MenuAudio::callback;
    wanted.userdata = &m_mixer;
    m_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (!m_device) {
        qWarning("[AUDIO] Menu audio is unavailable: %s", SDL_GetError());
        return;
    }

    for (const auto *name : {"move", "confirm", "back", "launch", "test_low", "test_high"}) m_clips.insert(name, load(name));
    m_mixer.setMusic(load("music"));
    m_mixer.setVolume(static_cast<float>(volume()) / 100.f);
    m_mixer.setMusicEnabled(musicEnabled());
    SDL_PauseAudioDevice(m_device, 0);
}

MenuAudio::~MenuAudio()
{
    if (m_device) SDL_CloseAudioDevice(m_device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void MenuAudio::callback(void *self, Uint8 *stream, int bytes)
{
    static_cast<MenuMixer *>(self)->render(reinterpret_cast<float *>(stream), bytes / (2 * static_cast<int>(sizeof(float))));
}

std::shared_ptr<const MenuMixer::Clip> MenuAudio::load(const QString &name) const
{
    QFile file(":/assets/qml/assets/sounds/" + name + ".wav");
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("[AUDIO] Missing menu sound: %s", qPrintable(name));
        return nullptr;
    }
    const auto bytes = file.readAll();
    SDL_AudioSpec spec{};
    Uint8 *samples = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV_RW(SDL_RWFromConstMem(bytes.constData(), static_cast<int>(bytes.size())), 1, &spec, &samples, &length)) {
        qWarning("[AUDIO] Unreadable menu sound %s: %s", qPrintable(name), SDL_GetError());
        return nullptr;
    }

    SDL_AudioCVT conversion;
    SDL_BuildAudioCVT(&conversion, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 2, MenuMixer::sampleRate);
    conversion.len = static_cast<int>(length);
    std::vector<Uint8> buffer(static_cast<std::size_t>(length) * std::max(conversion.len_mult, 1));
    std::memcpy(buffer.data(), samples, length);
    SDL_FreeWAV(samples);
    conversion.buf = buffer.data();
    if (conversion.needed && SDL_ConvertAudio(&conversion) != 0) return nullptr;

    const auto converted = conversion.needed ? conversion.len_cvt : conversion.len;
    auto clip = std::make_shared<MenuMixer::Clip>(static_cast<std::size_t>(converted) / sizeof(float) & ~std::size_t{1});
    std::memcpy(clip->data(), buffer.data(), clip->size() * sizeof(float));
    return clip;
}

int MenuAudio::volume() const
{
    return std::clamp(QSettings().value("audio/menuVolume", 60).toInt(), 0, 100);
}

void MenuAudio::setVolume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    QSettings().setValue("audio/menuVolume", volume);
    if (m_device) {
        SDL_LockAudioDevice(m_device);
        m_mixer.setVolume(static_cast<float>(volume) / 100.f);
        SDL_UnlockAudioDevice(m_device);
    }
    emit settingsChanged();
}

bool MenuAudio::musicEnabled() const
{
    return QSettings().value("audio/menuMusic", true).toBool();
}

void MenuAudio::setMusicEnabled(bool enabled)
{
    QSettings().setValue("audio/menuMusic", enabled);
    if (m_device) {
        SDL_LockAudioDevice(m_device);
        m_mixer.setMusicEnabled(enabled);
        SDL_UnlockAudioDevice(m_device);
    }
    emit settingsChanged();
}

void MenuAudio::setSpectrumActive(bool active)
{
    if (active == m_spectrumActive) return;
    m_spectrumActive = active;
    updateTimer();
    emit spectrumActiveChanged();
}

void MenuAudio::play(const QString &effect)
{
    if (!m_device) return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_gameRunning || now < m_quietUntil) return;
    const bool isMove = effect == QLatin1String("move");
    if (isMove && now - m_lastEffect < moveSpacingMs) return;
    m_lastEffect = now;

    SDL_LockAudioDevice(m_device);
    m_mixer.play(m_clips.value(effect));
    SDL_UnlockAudioDevice(m_device);
}

void MenuAudio::playTest(const QString &name)
{
    if (!m_device) return;
    SDL_LockAudioDevice(m_device);
    m_mixer.playTest(m_clips.value("test_" + name));
    SDL_UnlockAudioDevice(m_device);
    m_testPlaying = true;
    m_testProgress = 0;
    updateTimer();
    emit testChanged();
}

void MenuAudio::stopTest()
{
    if (!m_device) return;
    SDL_LockAudioDevice(m_device);
    m_mixer.stopTest();
    SDL_UnlockAudioDevice(m_device);
    refreshDisplay();
}

void MenuAudio::setGameRunning(bool running)
{
    m_gameRunning = running;
    if (!m_device) return;
    SDL_LockAudioDevice(m_device);
    m_mixer.setSuspended(running);
    m_mixer.stopTest();
    SDL_UnlockAudioDevice(m_device);
    if (running) {
        m_pauseAfterFade.start();
    } else {
        m_pauseAfterFade.stop();
        SDL_PauseAudioDevice(m_device, 0);
    }
    refreshDisplay();
}

void MenuAudio::updateTimer()
{
    if (m_spectrumActive || m_testPlaying) {
        m_display.start();
    } else {
        m_display.stop();
        refreshDisplay();
    }
}

void MenuAudio::refreshDisplay()
{
    std::vector<float> history(MenuMixer::historySize);
    SDL_LockAudioDevice(m_device);
    m_mixer.copyHistory(history.data());
    const bool playing = m_mixer.testPlaying();
    const double progress = m_mixer.testProgress();
    SDL_UnlockAudioDevice(m_device);

    if (playing != m_testPlaying || progress != m_testProgress) {
        m_testPlaying = playing;
        m_testProgress = progress;
        emit testChanged();
        if (!playing) updateTimer();
    }
    if (!m_spectrumActive) return;

    smoothLevels(m_levels, m_analyzer.analyze(history.data()));
    QVariantList display;
    display.reserve(bandCount);
    for (const auto level : m_levels) display.append(level);
    m_spectrum = display;
    emit spectrumChanged();
}
