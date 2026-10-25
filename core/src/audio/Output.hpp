#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>

namespace Core::Audio {
    class TimeStretcher;
    // Stereo signed PCM at 48 kHz. The host callback only consumes this bounded
    // queue; guest memory and guest callbacks stay on the emulation thread.
    class Output {
        public:
            static constexpr unsigned sampleRate = 48000, capacityFrames = 9600;
            static constexpr unsigned defaultLatencyFrames = 2880; // 60 ms, including time-stretcher delivery jitter
            struct Statistics {
                    std::uint64_t underruns{}, missingFrames{}, droppedFrames{}, clippedSamples{}, invalidSamples{};
                    unsigned queuedFrames{};
                    bool buffering{};
                    double tempo{1}, productionRate{1};
                    std::uint64_t sourceFrames{}, stretchedFrames{};
            };
            explicit Output(unsigned latencyFrames = defaultLatencyFrames, bool timeStretch = true);
            ~Output();
            bool start();
            void stop();
            void pause(bool paused);
            void setVolume(unsigned percent);
            void setLatencyFrames(unsigned frames);
            void setTimeStretchEnabled(bool enabled);
            void submit(std::span<const std::int16_t> stereo);
            void submit(std::span<const float> stereo);
            void read(std::span<std::int16_t> stereo);
            void clear();
            [[nodiscard]] unsigned queuedFrames() const;
            [[nodiscard]] Statistics statistics() const;

        private:
            template<typename Sample>
            void enqueue(std::span<const Sample> stereo);
            template<typename Sample>
            void append(std::span<const Sample> stereo); // mutex held
            void measureProduction(unsigned hostFrames); // callback updates control, never runs DSP
            void resetQueue(bool resetTempo = true); // mutex held
            mutable std::mutex mutex;
            std::array<float, capacityFrames * 2> samples{};
            unsigned begin{}, size{}, volume{100};
            unsigned latencyFrames{}, spliceRemaining{}, volumeRemaining{};
            float volumeGain{1}, volumeStep{};
            std::array<float, 2> lastOutput{}, spliceOrigin{};
            Statistics stats{};
            std::unique_ptr<TimeStretcher> stretcher;
            std::uint64_t meterInput{}, meterDemand{};
            bool stretchEnabled{}, meterStarted{}, meterMeasured{};
            std::uint32_t device{};
            bool ownsSubsystem{}, paused{};
    };
    Output &output();
} // namespace Core::Audio
