#include "MenuMixer.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr float musicMix = 0.45f; // Music stays behind the effects.
constexpr float musicFadeSeconds = 0.5f;
constexpr std::size_t maxEffects = 8;
}

void MenuMixer::play(std::shared_ptr<const Clip> clip)
{
    if (!clip) return;
    if (m_effects.size() >= maxEffects) m_effects.erase(m_effects.begin());
    m_effects.push_back({std::move(clip), 0});
}

void MenuMixer::playTest(std::shared_ptr<const Clip> clip)
{
    m_test = {std::move(clip), 0};
}

float MenuMixer::testProgress() const
{
    if (!m_test.clip || m_test.clip->empty()) return 0;
    return static_cast<float>(m_test.position) / static_cast<float>(m_test.clip->size());
}

void MenuMixer::render(float *stereo, int frames)
{
    // A test plays alone, so the graph and the listener get that sound only.
    const bool musicAudible = m_music && !m_music->empty() && m_musicEnabled && !m_suspended && !m_test.clip;
    const float musicTarget = musicAudible ? musicMix : 0.f;
    const float musicStep = 1.f / (musicFadeSeconds * sampleRate);
    const float gain = m_volume * m_volume; // Loudness grows far faster than amplitude falls.

    for (int frame = 0; frame < frames; ++frame) {
        float left = 0, right = 0;
        const auto addVoice = [&](Voice &voice) {
            left += (*voice.clip)[voice.position++];
            right += (*voice.clip)[voice.position++];
        };

        for (auto &voice : m_effects)
            if (voice.position < voice.clip->size()) addVoice(voice);
        if (m_test.clip) {
            addVoice(m_test);
            if (m_test.position >= m_test.clip->size()) m_test = {};
        }

        m_musicLevel += std::clamp(musicTarget - m_musicLevel, -musicStep, musicStep);
        if (m_music && !m_music->empty() && m_musicLevel > 0) {
            left += (*m_music)[m_musicPosition] * m_musicLevel;
            right += (*m_music)[m_musicPosition + 1] * m_musicLevel;
            m_musicPosition = (m_musicPosition + 2) % m_music->size();
        }

        left = std::clamp(left * gain, -1.f, 1.f);
        right = std::clamp(right * gain, -1.f, 1.f);
        stereo[2 * frame] = left;
        stereo[2 * frame + 1] = right;
        m_history[m_historyPosition] = (left + right) / 2;
        m_historyPosition = (m_historyPosition + 1) % historySize;
    }

    std::erase_if(m_effects, [](const Voice &voice) { return voice.position >= voice.clip->size(); });
}

void MenuMixer::copyHistory(float *mono) const
{
    std::copy(m_history.begin() + m_historyPosition, m_history.end(), mono);
    std::copy(m_history.begin(), m_history.begin() + m_historyPosition, mono + historySize - m_historyPosition);
}
