#include "audio/Output.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>

#include "audio/TimeStretcher.hpp"
#include "utils/Logger.hpp"

namespace Core::Audio {
    namespace {
        constexpr unsigned spliceFrames = 240; // 5 ms, shared by both channels
        constexpr unsigned volumeFrames = 480; // 10 ms control ramp
    } // namespace
    Output::Output(unsigned frames, bool timeStretch) : latencyFrames(std::min(frames, capacityFrames)), stretchEnabled(timeStretch)
    {
        if (stretchEnabled && latencyFrames)
            stretcher = std::make_unique<TimeStretcher>();
        stats.buffering = latencyFrames != 0;
    }
    Output::~Output() { stop(); }
    bool Output::start()
    {
        if (device)
            return true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            Utils::Log::error("[AUDIO] cannot initialize host audio: {}", SDL_GetError());
            return false;
        }
        ownsSubsystem = true;
        SDL_AudioSpec wanted{}, obtained{};
        wanted.freq = sampleRate;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 2;
        wanted.samples = 512;
        wanted.userdata = this;
        wanted.callback = [](void *context, Uint8 *stream, int bytes) {
            static_cast<Output *>(context)->read({reinterpret_cast<std::int16_t *>(stream), std::size_t(bytes) / 2});
        };
        device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
        if (!device) {
            Utils::Log::error("[AUDIO] cannot open host audio: {}", SDL_GetError());
            stop();
            return false;
        }
        SDL_PauseAudioDevice(device, paused);
        return true;
    }
    void Output::stop()
    {
        if (device)
            SDL_CloseAudioDevice(device);
        device = 0;
        if (ownsSubsystem)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        ownsSubsystem = false;
        clear();
    }
    void Output::pause(bool value)
    {
        if (device)
            SDL_PauseAudioDevice(device, value);
        std::lock_guard lock(mutex);
        paused = value;
        resetQueue(false);
    }
    void Output::setVolume(unsigned percent)
    {
        std::lock_guard lock(mutex);
        volume = std::min(percent, 100u);
        if (latencyFrames && !stats.buffering && !paused) {
            volumeStep = (float(volume) / 100.0f - volumeGain) / volumeFrames;
            volumeRemaining = volumeFrames;
        } else {
            volumeGain = float(volume) / 100.0f;
            volumeRemaining = 0;
        }
    }
    void Output::setLatencyFrames(unsigned frames)
    {
        std::lock_guard lock(mutex);
        latencyFrames = std::min(frames, capacityFrames);
        if (stretchEnabled && latencyFrames && !stretcher)
            stretcher = std::make_unique<TimeStretcher>();
        resetQueue();
    }
    void Output::setTimeStretchEnabled(bool enabled)
    {
        std::lock_guard lock(mutex);
        stretchEnabled = enabled;
        if (enabled && latencyFrames && !stretcher)
            stretcher = std::make_unique<TimeStretcher>();
        resetQueue();
    }
    template<typename Sample>
    void Output::enqueue(std::span<const Sample> stereo)
    {
        std::lock_guard lock(mutex);
        if (paused)
            return;
        stereo = stereo.first(stereo.size() & ~std::size_t{1});
        if (stereo.empty())
            return;
        stats.sourceFrames += stereo.size() / 2;
        meterStarted = true;
        meterInput += stereo.size() / 2;
        if (!stretchEnabled || !latencyFrames) {
            append(stereo);
            return;
        }
        // SoundTouch runs only on the producer. Bound every transfer so neither
        // emulation bursts nor slow playback can grow an intermediate queue.
        std::array<float, 2048> input{}, processed{};
        stretcher->setTempo(stats.tempo);
        while (!stereo.empty()) {
            const auto count = std::min(stereo.size(), input.size());
            for (unsigned i = 0; i < count; ++i) {
                const float value = stereo[i];
                if (!std::isfinite(value))
                    ++stats.invalidSamples;
                input[i] = std::isfinite(value) ? value : 0;
            }
            stretcher->submit(std::span<const float>(input).first(count));
            while (const auto frames = stretcher->receive(processed)) {
                stats.stretchedFrames += frames;
                append(std::span<const float>(processed).first(std::size_t(frames) * 2));
            }
            stereo = stereo.subspan(count);
        }
    }
    template<typename Sample>
    void Output::append(std::span<const Sample> stereo)
    {
        bool dropped = false;
        if (stereo.size() > samples.size()) {
            dropped = true;
            stats.droppedFrames += (stereo.size() - samples.size()) / 2;
            stereo = stereo.last(samples.size());
        }
        if (size + stereo.size() > samples.size()) {
            const auto discard = size + stereo.size() - samples.size();
            begin = (begin + discard) % samples.size();
            size -= discard;
            stats.droppedFrames += discard / 2;
            dropped = true;
        }
        if (dropped) {
            spliceOrigin = lastOutput;
            spliceRemaining = latencyFrames ? spliceFrames : 0;
        }
        for (const auto sample: stereo) {
            const float value = sample;
            if (!std::isfinite(value))
                ++stats.invalidSamples;
            samples[(begin + size) % samples.size()] = std::isfinite(value) ? value : 0;
            ++size;
        }
    }
    void Output::submit(std::span<const std::int16_t> stereo) { enqueue(stereo); }
    void Output::submit(std::span<const float> stereo) { enqueue(stereo); }
    void Output::measureProduction(unsigned frames)
    {
        if (!stretchEnabled || !latencyFrames || !meterStarted)
            return;
        meterDemand += frames;
        if (meterDemand < sampleRate / 10)
            return; // 100 ms, not the video FPS
        const double observed = std::clamp(double(meterInput) / double(meterDemand), .25, 1.25);
        stats.productionRate = meterMeasured ? stats.productionRate * .75 + observed * .25 : observed;
        // Slow feedback corrects queue drift without periodically inserting
        // silence. Source pitch and the guest clock are untouched.
        const double errorSeconds = ((double(size) / 2) - latencyFrames) / sampleRate;
        const double target = std::clamp(stats.productionRate + std::clamp(errorSeconds * .5, -.06, .06), .25, 1.25);
        stats.tempo = meterMeasured ? stats.tempo * .5 + target * .5 : target;
        meterMeasured = true;
        meterInput = meterDemand = 0;
    }
    void Output::read(std::span<std::int16_t> stereo)
    {
        std::lock_guard lock(mutex);
        std::fill(stereo.begin(), stereo.end(), 0);
        if (paused || stereo.size() < 2)
            return;
        const auto requested = stereo.size() / 2;
        measureProduction(requested);
        if (stats.buffering) {
            if (size / 2 < std::max<std::size_t>(latencyFrames, std::min<std::size_t>(requested, capacityFrames)))
                return;
            stats.buffering = false;
            spliceOrigin = {};
            spliceRemaining = latencyFrames ? spliceFrames : 0;
        }
        const auto frames = std::min<std::size_t>(size / 2, requested);
        const bool underrun = frames < requested;
        const auto fadeOut = latencyFrames && underrun ? std::min<std::size_t>(spliceFrames, frames) : 0;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (volumeRemaining) {
                volumeGain += volumeStep;
                if (!--volumeRemaining)
                    volumeGain = float(volume) / 100.0f;
            }
            const float blend = spliceRemaining ? float(spliceFrames - spliceRemaining + 1) / spliceFrames : 1;
            const float tail = fadeOut && frame >= frames - fadeOut ? float(frames - frame - 1) / float(fadeOut) : 1;
            for (unsigned ch = 0; ch < 2; ++ch) {
                float value = samples[(begin + frame * 2 + ch) % samples.size()] * volumeGain;
                if (spliceRemaining)
                    value = spliceOrigin[ch] + (value - spliceOrigin[ch]) * blend;
                value *= tail;
                if (value < -32768 || value > 32767)
                    ++stats.clippedSamples;
                lastOutput[ch] = std::clamp(value, -32768.0f, 32767.0f);
                stereo[frame * 2 + ch] = static_cast<std::int16_t>(lastOutput[ch]);
            }
            if (spliceRemaining)
                --spliceRemaining;
        }
        begin = (begin + frames * 2) % samples.size();
        size -= frames * 2;
        if (underrun) {
            // A fully supplied callback is never an underrun, even when it
            // happens to leave the queue empty. The next packet may be on time.
            // With no real samples at all, fade the previous endpoint once.
            if (latencyFrames && !frames) {
                const auto tail = std::min<std::size_t>(spliceFrames, requested);
                for (std::size_t i = 0; i < tail; ++i)
                    for (unsigned ch = 0; ch < 2; ++ch)
                        stereo[i * 2 + ch] = static_cast<std::int16_t>(lastOutput[ch] * (1 - float(i + 1) / float(tail)));
            }
            ++stats.underruns;
            stats.missingFrames += requested - frames;
            stats.buffering = latencyFrames != 0;
            lastOutput = {};
        }
    }
    void Output::resetQueue(bool resetTempo)
    {
        begin = size = spliceRemaining = volumeRemaining = 0;
        lastOutput = spliceOrigin = {};
        volumeGain = float(volume) / 100.0f;
        stats.buffering = latencyFrames != 0;
        if (resetTempo) {
            stats.tempo = stats.productionRate = 1;
            meterMeasured = false;
        }
        meterInput = meterDemand = 0;
        meterStarted = false;
        if (stretcher) {
            stretcher->reset();
            stretcher->setTempo(stats.tempo);
        }
    }
    void Output::clear()
    {
        std::lock_guard lock(mutex);
        resetQueue();
    }
    Output::Statistics Output::statistics() const
    {
        std::lock_guard lock(mutex);
        auto result = stats;
        result.queuedFrames = size / 2;
        return result;
    }
    unsigned Output::queuedFrames() const
    {
        std::lock_guard lock(mutex);
        return size / 2;
    }
    Output &output()
    {
        static Output instance;
        return instance;
    }
} // namespace Core::Audio
