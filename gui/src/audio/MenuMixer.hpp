#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <vector>

// Mixes WEMU's own sounds: short effects, a looping music track and a test sound.
// Nothing here touches an audio device, and the caller keeps render() and the other calls apart.
class MenuMixer {
public:
    using Clip = std::vector<float>; // Interleaved stereo.
    static constexpr int sampleRate = 48000;
    static constexpr std::size_t historySize = 2048;

    void setMusic(std::shared_ptr<const Clip> music) { m_music = std::move(music); }
    void play(std::shared_ptr<const Clip> clip);
    void playTest(std::shared_ptr<const Clip> clip);
    void stopTest() { m_test = {}; }

    void setVolume(float volume) { m_volume = volume; }
    void setMusicEnabled(bool enabled) { m_musicEnabled = enabled; }
    // While a game runs, the music fades away and effects already playing may finish.
    void setSuspended(bool suspended) { m_suspended = suspended; }

    [[nodiscard]] bool testPlaying() const { return m_test.clip != nullptr; }
    [[nodiscard]] float testProgress() const;

    void render(float *stereo, int frames);
    // The newest output after volume, oldest first: what the listener should be hearing.
    void copyHistory(float *mono) const;

private:
    struct Voice {
        std::shared_ptr<const Clip> clip;
        std::size_t position = 0;
    };

    std::vector<Voice> m_effects;
    Voice m_test;
    std::shared_ptr<const Clip> m_music;
    std::size_t m_musicPosition = 0;
    float m_musicLevel = 0;
    float m_volume = 1;
    bool m_musicEnabled = true;
    bool m_suspended = false;
    std::array<float, historySize> m_history{};
    std::size_t m_historyPosition = 0;
};
