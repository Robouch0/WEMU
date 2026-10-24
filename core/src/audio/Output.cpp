#include "audio/Output.hpp"

#include <SDL.h>
#include <algorithm>

#include "utils/Logger.hpp"

namespace Core::Audio {
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
        begin = size = 0;
    }
    void Output::setVolume(unsigned percent)
    {
        std::lock_guard lock(mutex);
        volume = std::min(percent, 100u);
    }
    void Output::submit(std::span<const std::int16_t> stereo)
    {
        std::lock_guard lock(mutex);
        if (paused)
            return;
        // Keep stereo alignment and discard oldest audio when emulation outruns
        // playback. Never block the CPU or grow latency without a bound.
        stereo = stereo.first(stereo.size() & ~std::size_t{1});
        if (stereo.size() > samples.size())
            stereo = stereo.last(samples.size());
        if (size + stereo.size() > samples.size()) {
            const auto discard = size + stereo.size() - samples.size();
            begin = (begin + discard) % samples.size();
            size -= discard;
        }
        for (const auto sample: stereo) {
            samples[(begin + size) % samples.size()] = sample;
            ++size;
        }
    }
    void Output::read(std::span<std::int16_t> stereo)
    {
        std::lock_guard lock(mutex);
        std::fill(stereo.begin(), stereo.end(), 0);
        if (paused)
            return;
        const auto count = std::min<std::size_t>(size, stereo.size() & ~std::size_t{1});
        for (std::size_t i = 0; i < count; ++i)
            stereo[i] = std::int32_t(samples[(begin + i) % samples.size()]) * std::int32_t(volume) / 100;
        begin = (begin + count) % samples.size();
        size -= count;
    }
    void Output::clear()
    {
        std::lock_guard lock(mutex);
        begin = size = 0;
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
