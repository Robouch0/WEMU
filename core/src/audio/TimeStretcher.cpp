#include "audio/TimeStretcher.hpp"

#include <SoundTouch.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Core::Audio {
    struct TimeStretcher::Impl {
            soundtouch::SoundTouch processor;
            double tempo{1};
    };
    TimeStretcher::TimeStretcher() : impl(std::make_unique<Impl>())
    {
        auto &processor = impl->processor;
        processor.setSampleRate(48000);
        processor.setChannels(2);
        processor.setSetting(SETTING_USE_QUICKSEEK, 1);
        processor.setSetting(SETTING_SEQUENCE_MS, 40);
        processor.setSetting(SETTING_SEEKWINDOW_MS, 15);
        processor.setSetting(SETTING_OVERLAP_MS, 8);
        processor.setTempo(1);
    }
    TimeStretcher::~TimeStretcher() = default;
    void TimeStretcher::reset()
    {
        impl->processor.clear();
        impl->processor.setTempo(1);
        impl->tempo = 1;
    }
    void TimeStretcher::setTempo(double tempo)
    {
        if (!std::isfinite(tempo) || tempo < .25 || tempo > 1.25)
            throw std::invalid_argument("Audio tempo is outside the supported range");
        if (std::abs(tempo - impl->tempo) >= .0001) {
            impl->processor.setTempo(tempo);
            impl->tempo = tempo;
        }
    }
    void TimeStretcher::submit(std::span<const float> stereo)
    {
        if (stereo.size() % 2)
            throw std::invalid_argument("Incomplete stereo audio frame");
        std::array<float, 2048> normalized{};
        while (!stereo.empty()) {
            const auto count = std::min(stereo.size(), normalized.size());
            for (unsigned i = 0; i < count; ++i)
                normalized[i] = std::clamp(stereo[i], -2147483648.0f, 2147483648.0f) / 32768.0f;
            impl->processor.putSamples(normalized.data(), count / 2);
            stereo = stereo.subspan(count);
        }
    }
    unsigned TimeStretcher::receive(std::span<float> stereo)
    {
        const auto frames = impl->processor.receiveSamples(stereo.data(), stereo.size() / 2);
        for (unsigned i = 0; i < frames * 2; ++i)
            stereo[i] *= 32768;
        return frames;
    }
} // namespace Core::Audio
