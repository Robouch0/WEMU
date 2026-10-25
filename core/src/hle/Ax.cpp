// AX HLE: guest-clock voice mixing, effect callbacks and buffered host output.

#include "Ax.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include "audio/Output.hpp"
#include "audio/Resampler.hpp"
#include "audio/Voice.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/MvPlayer.hpp"
#include "hle/Scheduler.hpp"
#include "utils/Logger.hpp"

namespace {

    constexpr std::uint32_t kMaxVoices = 96;
    constexpr std::uint32_t kVoiceSize = 0x58; // public AXVoice layout (WUT sndcore2/voice.h)
    constexpr std::uint32_t kVoiceOffsets = 0x34;
    constexpr std::uint32_t kVoiceOffsetsSize = 0x14;

    struct VoiceSlot : Core::Audio::Voice {
            std::uint32_t guestPtr{};
            bool used{};
    };
    std::vector<VoiceSlot> g_voices;
    using Core::Audio::addressUnits;
    constexpr std::uint32_t kStateOffset = 0x04;
    constexpr unsigned kSamples = 144;
    unsigned g_inputSamples = 96;
    unsigned channels(unsigned type) { return type == 0 ? 6 : 4; }
    unsigned devices(unsigned type) { return type == 0 ? 1 : 2; }
    bool validDevice(unsigned type, unsigned id) { return type < 2 && id < devices(type); }
    using Plane = std::array<float, kSamples>;
    // [type][device][bus][channel][sample]
    std::array<std::array<std::array<std::array<Plane, 6>, 4>, 2>, 2> g_buses{};
    struct Aux {
            std::uint32_t callback{}, userData{}, storage{};
            std::uint16_t volume{0x8000};
            bool pending{};
    };
    std::array<std::array<std::array<Aux, 3>, 2>, 2> g_aux{};
    std::array<std::array<std::uint16_t, 2>, 2> g_deviceVolume{{{0x8000, 0x8000}, {0x8000, 0x8000}}};
    std::array<bool, 2> g_upsampleAfter{};
    std::array<unsigned, 2> g_deviceMode{0, 0}; // stereo

    VoiceSlot *findVoice(std::uint32_t guestPtr)
    {
        for (auto &slot: g_voices)
            if (slot.used && slot.guestPtr == guestPtr)
                return &slot;
        return nullptr;
    }

    void writeVoiceOffsets(Core::Interpreter &cpu, const VoiceSlot &voice);

    std::vector<std::uint32_t> g_appFrameCallbacks; // fired each audio frame by the pump below
    std::uint32_t g_frameCallback = 0;
    bool g_initialized = false;
    struct FinalMixDevice {
            std::uint32_t callback = 0;
            std::uint32_t storage = 0;
            bool pending = false;
            unsigned samples = kSamples;
            std::array<Core::Audio::LinearUpsampler, 8> upsamplers{};
    };
    std::array<FinalMixDevice, 2> g_finalMix;

    // ---- frame-callback pump ------------------------------------------------------------------
    // A synthetic guest thread runs each 3 ms audio frame on the emulated time base.
    // Quarter ticks represent the 186468.75-tick period without rounding drift.

    constexpr std::int32_t kAxThreadPriority = 0; // audio callbacks preempt everything, like the AX ISR
    constexpr std::uint32_t kAxStackSize = 0x10000;
    constexpr std::uint64_t kFrameQuarterTicks = 745875;
    std::uint64_t g_nextFrameQuarterTicks = 0;
    std::uint64_t g_frameCount = 0;
    std::uint64_t g_firstFrameTick = 0;

    Core::ThreadContext *g_axThread = nullptr;
    std::uint32_t g_axStackTop = 0;
    struct PendingCallback {
            std::uint32_t function;
            std::uint32_t argument = 0, argument2 = 0, argument3 = 0;
    };
    std::vector<PendingCallback> g_pendingCbs;
    std::size_t g_pendingIdx = 0;

    unsigned g_phase = 3;


    // Aux/final buffers are signed 32-bit containers in PCM16 amplitude units.
    // Keep summing headroom until host volume has been applied.
    std::int32_t mixedSample(float value)
    {
        if (!std::isfinite(value))
            return 0;
        return static_cast<std::int32_t>(std::clamp(double(value), double(INT32_MIN), double(INT32_MAX)));
    }

    void mixVoices(Core::Interpreter &cpu)
    {
        g_buses = {};
        for (auto &voice: g_voices) {
            if (!voice.used || voice.state != 1)
                continue;
            std::array<float, kSamples> mono{};
            for (unsigned i = 0; i < g_inputSamples; ++i)
                mono[i] = voice.render(cpu.m_memory);
            for (unsigned type = 0; type < 2; ++type)
                for (unsigned id = 0; id < devices(type); ++id)
                    for (unsigned ch = 0; ch < channels(type); ++ch)
                        for (unsigned bus = 0; bus < 4; ++bus) {
                            auto &gain = voice.gains[type][id][ch][bus];
                            if (!gain.volume && !gain.delta)
                                continue;
                            auto &destination = g_buses[type][id][bus][ch];
                            for (unsigned i = 0; i < g_inputSamples; ++i) {
                                destination[i] += mono[i] * (float(gain.volume) / 32768.0f);
                                gain.volume = std::clamp(gain.volume + gain.delta, 0, 65535);
                            }
                        }
            cpu.m_memory.write<std::uint32_t>(voice.guestPtr + kStateOffset, voice.state);
            cpu.m_memory.write<std::uint32_t>(voice.guestPtr + 8, voice.envelope);
            writeVoiceOffsets(cpu, voice);
        }
    }

    void prepareAux(Core::Interpreter &cpu)
    {
        mixVoices(cpu);
        for (unsigned type = 0; type < 2; ++type)
            for (unsigned id = 0; id < devices(type); ++id)
                for (unsigned bus = 0; bus < 3; ++bus) {
                    auto &aux = g_aux[type][id][bus];
                    aux.pending = aux.callback != 0;
                    if (!aux.pending)
                        continue;
                    const auto count = channels(type);
                    if (!aux.storage)
                        aux.storage = cpu.m_memory.heapAllocate(8 + count * 4 + count * kSamples * 4, 32);
                    const auto pointers = aux.storage + 8;
                    cpu.m_memory.write<std::uint32_t>(aux.storage, count);
                    cpu.m_memory.write<std::uint32_t>(aux.storage + 4, g_inputSamples);
                    for (unsigned ch = 0; ch < count; ++ch) {
                        const auto data = pointers + count * 4 + ch * kSamples * 4;
                        cpu.m_memory.write<std::uint32_t>(pointers + ch * 4, data);
                        for (unsigned i = 0; i < g_inputSamples; ++i)
                            cpu.m_memory.write<std::int32_t>(data + i * 4, mixedSample(g_buses[type][id][bus + 1][ch][i]));
                    }
                    g_pendingCbs.push_back({aux.callback, pointers, aux.userData, aux.storage});
                }
    }

    void prepareFinal(Core::Interpreter &cpu)
    {
        std::array<float, std::size_t(kSamples) * 2> movie{};
        Core::MvPlayer::mixAudio(cpu, movie, (g_nextFrameQuarterTicks - 2 * kFrameQuarterTicks) / 4);
        for (unsigned type = 0; type < 2; ++type) {
            auto &mix = g_finalMix[type];
            const auto count = channels(type), deviceCount = devices(type);
            for (unsigned id = 0; id < deviceCount; ++id)
                for (unsigned ch = 0; ch < count; ++ch) {
                    auto &main = g_buses[type][id][0][ch];
                    for (unsigned i = 0; i < g_inputSamples; ++i) {
                        for (unsigned bus = 0; bus < 3; ++bus) {
                            const auto &aux = g_aux[type][id][bus];
                            // An unregistered effect bus has no wet return.
                            // Passing its dry send through again doubles voices.
                            if (aux.pending) {
                                const float value = float(cpu.m_memory.read<std::int32_t>(aux.storage + 8 + count * 4 + ch * kSamples * 4 + i * 4));
                                main[i] += value * (float(aux.volume) / 32768.0f);
                            }
                        }
                        main[i] *= float(g_deviceVolume[type][id]) / 32768.0f;
                    }
                    if (!g_upsampleAfter[type] && g_inputSamples == 96) {
                        const auto input = main;
                        mix.upsamplers[id * count + ch].convert(std::span<const float>(input).first(96), main);
                    }
                }
            mix.samples = g_upsampleAfter[type] ? g_inputSamples : kSamples;
            if (type == 0) {
                for (unsigned i = 0; i < mix.samples; ++i) {
                    const float position = float(i) * (float(kSamples) / float(mix.samples));
                    const auto lo = static_cast<unsigned>(position);
                    const unsigned hi = std::min(lo + 1, kSamples - 1);
                    for (unsigned ch = 0; ch < 2; ++ch) {
                        const float value = movie[lo * 2 + ch] + (movie[hi * 2 + ch] - movie[lo * 2 + ch]) * (position - float(lo));
                        g_buses[0][0][0][ch][i] += value * (float(g_deviceVolume[0][0]) / 32768.0f);
                    }
                }
            }
            mix.pending = mix.callback != 0;
            if (!mix.pending)
                continue;
            const auto planes = count * deviceCount;
            if (!mix.storage)
                mix.storage = cpu.m_memory.heapAllocate(12 + planes * 4 + planes * kSamples * 4, 32);
            const auto pointers = mix.storage + 12;
            cpu.m_memory.write<std::uint32_t>(mix.storage, pointers);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 4, count);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 6, mix.samples);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 8, deviceCount);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 10, count);
            for (unsigned plane = 0; plane < planes; ++plane) {
                const auto data = pointers + planes * 4 + plane * kSamples * 4;
                cpu.m_memory.write<std::uint32_t>(pointers + plane * 4, data);
                for (unsigned i = 0; i < mix.samples; ++i)
                    cpu.m_memory.write<std::int32_t>(data + i * 4, mixedSample(g_buses[type][plane / count][0][plane % count][i]));
            }
            g_pendingCbs.push_back({mix.callback, mix.storage});
        }
    }

    void submitFrame(Core::Interpreter &cpu)
    {
        // TV device 0 feeds the desktop speakers. DRC buffers and effects are
        // rendered independently, without doubling games' mirrored TV sound.
        auto &mix = g_finalMix[0];
        const unsigned count = channels(0);
        const unsigned outputChannels = mix.pending ? std::clamp<unsigned>(cpu.m_memory.read<std::uint16_t>(mix.storage + 10), 1, count) : count;
        const auto fetch = [&](unsigned ch, unsigned index) -> float {
            if (ch >= outputChannels)
                return 0;
            if (mix.pending)
                return float(cpu.m_memory.read<std::int32_t>(mix.storage + 12 + count * 4 + ch * kSamples * 4 + index * 4));
            return g_buses[0][0][0][ch][index];
        };
        std::array<Plane, 6> planes{};
        for (unsigned ch = 0; ch < outputChannels; ++ch) {
            if (mix.samples == 96) {
                std::array<float, 96> input{};
                for (unsigned i = 0; i < 96; ++i)
                    input[i] = fetch(ch, i);
                mix.upsamplers[ch].convert(input, planes[ch]);
            } else
                for (unsigned i = 0; i < kSamples; ++i)
                    planes[ch][i] = fetch(ch, i);
        }
        std::array<float, std::size_t(kSamples) * 2> stereo{};
        unsigned overrange = 0;
        float peak = 0;
        for (std::size_t i = 0; i < kSamples; ++i) {
            if (outputChannels == 1 || g_deviceMode[0] == 5) {
                stereo[i * 2] = stereo[i * 2 + 1] = planes[0][i];
            } else if (g_deviceMode[0] == 3 && outputChannels > 2) {
                // AX order: L, R, surround L/R, center, LFE. Normalized
                // coefficients reserve headroom. LFE is not a full-range send.
                constexpr float surround = .70710678f, headroom = 1 / (1 + 2 * surround);
                stereo[i * 2] = (planes[0][i] + surround * (planes[2][i] + planes[4][i])) * headroom;
                stereo[i * 2 + 1] = (planes[1][i] + surround * (planes[3][i] + planes[4][i])) * headroom;
            } else {
                // Stereo (and matrix-encoded surround) is already mixed by the
                // title. Extra callback planes must not boost its bass/volume.
                stereo[i * 2] = planes[0][i];
                stereo[i * 2 + 1] = planes[1][i];
            }
            for (unsigned ch = 0; ch < 2; ++ch) {
                const float value = stereo[i * 2 + ch];
                peak = std::max(peak, std::abs(value));
                overrange += value > 32767 || value < -32768;
            }
        }
        Core::Audio::output().submit(stereo);
        if (std::getenv("WEMU_AX_TRACE") && (g_frameCount <= 8 || g_frameCount % 256 == 0)) {
            unsigned active = 0;
            for (const auto &v: g_voices)
                active += v.used && v.state == 1;
            const auto stats = Core::Audio::output().statistics();
            Utils::Log::error("[AX_MIX] frame={} active={} peak={} overrange={} queued_frames={} buffering={} underruns={} missing_frames={} "
                              "dropped_frames={} clipped_samples={} tempo={} production_rate={}",
                              g_frameCount, active, peak, overrange, stats.queuedFrames, stats.buffering, stats.underruns, stats.missingFrames,
                              stats.droppedFrames, stats.clippedSamples, stats.tempo, stats.productionRate);
        }
    }

    // App callbacks update voices first, then aux callbacks process their
    // buses, then final callbacks see the resulting mix. Submit only on return.
    std::optional<PendingCallback> nextCallback(Core::Interpreter &cpu)
    {
        while ((g_initialized || Core::MvPlayer::hasPlayingAudio(cpu)) && g_pendingIdx == g_pendingCbs.size() && g_phase < 3) {
            switch (g_phase++) {
                case 0:
                    prepareAux(cpu);
                    break;
                case 1:
                    prepareFinal(cpu);
                    break;
                case 2:
                    submitFrame(cpu);
                    break;
                default:
                    break; // The loop guard restricts g_phase to 0..2.
            }
        }
        if ((!g_initialized && !Core::MvPlayer::hasPlayingAudio(cpu)) || g_pendingIdx == g_pendingCbs.size())
            return {};
        return g_pendingCbs[g_pendingIdx++];
    }

    void buildPending(Core::Interpreter &cpu)
    {
        if (!g_frameCount)
            g_firstFrameTick = cpu.m_scheduler.now();
        ++g_frameCount;
        if (std::getenv("WEMU_AX_TRACE") && (g_frameCount <= 8 || g_frameCount % 256 == 0))
            Utils::Log::error("[AX_CLOCK] frame={} tick={} elapsed_ticks={}", g_frameCount, cpu.m_scheduler.now(),
                              cpu.m_scheduler.now() - g_firstFrameTick);
        g_nextFrameQuarterTicks += kFrameQuarterTicks;
        g_pendingCbs.clear();
        g_pendingIdx = 0;
        g_phase = 0;
        if (g_frameCallback)
            g_pendingCbs.push_back({g_frameCallback});
        for (auto callback: g_appFrameCallbacks)
            g_pendingCbs.push_back({callback});
    }

    void ensureAxThread(Core::Interpreter &cpu)
    {
        if (g_axThread)
            return;
        constexpr std::uint32_t OS_THREAD_SIZE = 0x680;
        constexpr std::uint32_t OS_THREAD_TAG_OFFSET = 0x32C;
        const std::uint32_t osThread = cpu.m_memory.heapAllocate(OS_THREAD_SIZE, 8);
        for (std::uint32_t off = 0; off < OS_THREAD_SIZE; off += 4)
            cpu.m_memory.write<std::uint32_t>(osThread + off, 0);
        cpu.m_memory.write<std::uint32_t>(osThread + OS_THREAD_TAG_OFFSET, 0x74487244u); // 'tHrD'
        g_axStackTop = cpu.m_memory.heapAllocate(kAxStackSize, 0x10) + kAxStackSize;
        g_axThread = cpu.m_scheduler.create(cpu, osThread, Core::AX_FRAME_SENTINEL, 0, 0, g_axStackTop, kAxThreadPriority);
        g_axThread->name = "AXFrame";
        // Its own core: on hardware the AX ISR runs beside the app cores, so a callback that
        // spins on guest state must not starve the threads that will satisfy the spin. The
        // disjoint-affinity preempt rule lets the scheduler timeslice around it.
        g_axThread->affinity = 4;
        Utils::Log::error("[AX] frame pump thread created (handle=0x{:08X})", osThread);
        // create() leaves it Paused, which doubles as our "parked" state.
    }

} // namespace

namespace Core::Ax {

    void StartMovieAudio(Core::Interpreter &cpu)
    {
        if (!g_initialized)
            g_nextFrameQuarterTicks = cpu.m_scheduler.now() * 4 + kFrameQuarterTicks;
    }

    void OnFrameTick(Core::Interpreter &cpu)
    {
        if ((!g_initialized && !Core::MvPlayer::hasPlayingAudio(cpu)) || cpu.m_scheduler.now() * 4 < g_nextFrameQuarterTicks)
            return;
        ensureAxThread(cpu);
        if (g_axThread->state != Core::ThreadContext::State::Paused)
            return; // previous frame still in flight; skip this one
        buildPending(cpu);
        Core::ThreadContext *t = g_axThread;
        const auto callback = nextCallback(cpu);
        t->pc = (callback ? callback->function : Core::AX_FRAME_SENTINEL) - Core::Memory::MemoryMap::ApplicationCode;
        t->gpr[3] = callback ? callback->argument : 0;
        t->gpr[4] = callback ? callback->argument2 : 0;
        t->gpr[5] = callback ? callback->argument3 : 0;
        t->lr = Core::AX_FRAME_SENTINEL - Core::Memory::MemoryMap::ApplicationCode;
        t->gpr[1] = (g_axStackTop & ~0xFu) - 8;
        t->gpr[2] = cpu.m_gpr[2]; // SDA bases (shared address space)
        t->gpr[13] = cpu.m_gpr[13];
        t->state = Core::ThreadContext::State::Ready;
        // The scheduler's next preempt/switch point picks it up (priority 0 beats everything).
    }

    void OnSentinelReturn(Core::Interpreter &cpu)
    {
        const std::uint32_t appCode = Core::Memory::MemoryMap::ApplicationCode;
        if (const auto callback = nextCallback(cpu)) {
            cpu.m_pc = callback->function - appCode;
            cpu.m_gpr[3] = callback->argument;
            cpu.m_gpr[4] = callback->argument2;
            cpu.m_gpr[5] = callback->argument3;
            cpu.m_nextPc = cpu.m_pc;
            cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
            cpu.m_gpr[1] = (g_axStackTop & ~0xFu) - 8;
            return;
        }
        g_pendingCbs.clear();
        g_pendingIdx = 0;
        if (!g_initialized && !Core::MvPlayer::hasPlayingAudio(cpu)) {
            if (cpu.m_scheduler.parkCurrent(cpu))
                return;
            cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
            cpu.m_pc = cpu.m_nextPc = cpu.m_lr;
            cpu.m_scheduler.sleep(cpu, 1000);
            return;
        }
        // Keep the audio deadline visible to the scheduler even when all app
        // threads sleep. Late service catches up without discarding sample frames.
        if (cpu.m_scheduler.now() * 4 < g_nextFrameQuarterTicks) {
            cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
            cpu.m_pc = cpu.m_nextPc = cpu.m_lr;
            cpu.m_scheduler.sleep(cpu, (g_nextFrameQuarterTicks - cpu.m_scheduler.now() * 4 + 3) / 4);
            return;
        }
        buildPending(cpu);
        if (const auto callback = nextCallback(cpu)) {
            cpu.m_pc = callback->function - appCode;
            cpu.m_gpr[3] = callback->argument;
            cpu.m_gpr[4] = callback->argument2;
            cpu.m_gpr[5] = callback->argument3;
            cpu.m_nextPc = cpu.m_pc;
            cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
            cpu.m_gpr[1] = (g_axStackTop & ~0xFu) - 8;
            return;
        }
        cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
        cpu.m_pc = cpu.m_nextPc = cpu.m_lr;
        cpu.m_scheduler.sleep(
                cpu,
                std::max<std::uint64_t>(
                        1, (g_nextFrameQuarterTicks > cpu.m_scheduler.now() * 4 ? g_nextFrameQuarterTicks - cpu.m_scheduler.now() * 4 + 3 : 4) / 4));
    }

} // namespace Core::Ax

namespace {

    void ax_noop_ok(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    void ax_Init(Core::Interpreter &cpu)
    {
        if (g_initialized) {
            cpu.m_gpr[3] = 0;
            return;
        }
        g_initialized = true;
        g_inputSamples = 96;
        for (auto &mix: g_finalMix)
            for (auto &converter: mix.upsamplers)
                converter.reset();
        g_nextFrameQuarterTicks = cpu.m_scheduler.now() * 4 + kFrameQuarterTicks;
        cpu.m_gpr[3] = 0;
    }

    void ax_Quit(Core::Interpreter &cpu)
    {
        g_initialized = false;
        for (auto &v: g_voices) {
            v.state = 0;
            if (v.used)
                cpu.m_memory.write<std::uint32_t>(v.guestPtr + kStateOffset, 0);
            v.used = false;
        }
        g_frameCallback = 0;
        g_appFrameCallbacks.clear();
        for (auto &mix: g_finalMix)
            mix.callback = 0;
        for (auto &group: g_aux)
            for (auto &device: group)
                for (auto &aux: device)
                    aux.callback = 0;
        g_pendingCbs.clear();
        g_pendingIdx = 0;
        g_phase = 3;
        Core::Audio::output().clear();
        cpu.m_gpr[3] = 0;
    }

    void ax_RegisterDeviceFinalMixCallback(Core::Interpreter &cpu)
    {
        const auto device = cpu.m_gpr[3];
        if (device >= g_finalMix.size()) {
            cpu.m_gpr[3] = static_cast<std::uint32_t>(-1);
            return;
        }
        g_finalMix[device].callback = cpu.m_gpr[4];
        cpu.m_gpr[3] = 0;
    }

    void ax_GetDeviceFinalMixCallback(Core::Interpreter &cpu)
    {
        const auto device = cpu.m_gpr[3];
        if (device >= g_finalMix.size() || !cpu.m_gpr[4]) {
            cpu.m_gpr[3] = static_cast<std::uint32_t>(-1);
            return;
        }
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], g_finalMix[device].callback);
        cpu.m_gpr[3] = 0;
    }

    // AXVoice *AXAcquireVoice(u32 priority, AXVoiceCallbackFn cb, void *userdata)
    void ax_AcquireVoice(Core::Interpreter &cpu)
    {
        for (std::uint32_t i = 0; i < kMaxVoices; i++) {
            if (i >= g_voices.size())
                g_voices.push_back({});
            VoiceSlot &slot = g_voices[i];
            if (slot.used)
                continue;
            if (!slot.guestPtr)
                slot.guestPtr = cpu.m_memory.heapAllocate(kVoiceSize, 0x20);
            for (std::uint32_t off = 0; off < kVoiceSize; off += 4)
                cpu.m_memory.write<std::uint32_t>(slot.guestPtr + off, 0);
            cpu.m_memory.write<std::uint32_t>(slot.guestPtr + 0x00, i); // index
            cpu.m_memory.write<std::uint32_t>(slot.guestPtr + 0x1C, cpu.m_gpr[3]);
            cpu.m_memory.write<std::uint32_t>(slot.guestPtr + 0x20, cpu.m_gpr[4]);
            cpu.m_memory.write<std::uint32_t>(slot.guestPtr + 0x24, cpu.m_gpr[5]);
            const auto pointer = slot.guestPtr;
            slot = {};
            slot.guestPtr = pointer;
            slot.used = true;
            cpu.m_gpr[3] = slot.guestPtr;
            return;
        }
        Utils::Log::error("[AX] out of voices");
        cpu.m_gpr[3] = 0;
    }

    void ax_AcquireVoiceEx(Core::Interpreter &cpu)
    {
        const auto callback = cpu.m_gpr[4];
        ax_AcquireVoice(cpu);
        if (const auto voice = cpu.m_gpr[3]) {
            cpu.m_memory.write<std::uint32_t>(voice + 0x20, 0);
            cpu.m_memory.write<std::uint32_t>(voice + 0x48, callback);
        }
    }
    void ax_SetVoiceSamplesAddr(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3])) {
            v->base = cpu.m_gpr[4];
            cpu.m_memory.write<std::uint32_t>(v->guestPtr + kVoiceOffsets + 16, v->base);
            writeVoiceOffsets(cpu, *v);
        }
        cpu.m_gpr[3] = 0;
    }

    void ax_FreeVoice(Core::Interpreter &cpu)
    {
        for (auto &slot: g_voices)
            if (slot.guestPtr == cpu.m_gpr[3]) {
                slot.used = false;
                slot.state = 0;
                cpu.m_memory.write<std::uint32_t>(slot.guestPtr + kStateOffset, 0);
            }
        cpu.m_gpr[3] = 0;
    }

    // AXRegisterAppFrameCallback(fn) -> 0; fired each frame by the pump.
    void ax_RegisterAppFrameCallback(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3]) {
            if (std::find(g_appFrameCallbacks.begin(), g_appFrameCallbacks.end(), cpu.m_gpr[3]) == g_appFrameCallbacks.end())
                g_appFrameCallbacks.push_back(cpu.m_gpr[3]);
            Utils::Log::error("[AX] app frame callback registered: 0x{:08X}", cpu.m_gpr[3]);
        }
        cpu.m_gpr[3] = 0;
    }

    void ax_DeregisterAppFrameCallback(Core::Interpreter &cpu)
    {
        std::erase(g_appFrameCallbacks, cpu.m_gpr[3]);
        cpu.m_gpr[3] = 0;
    }

    // AXRegisterFrameCallback(fn) -> previous callback.
    void ax_RegisterFrameCallback(Core::Interpreter &cpu)
    {
        const std::uint32_t prev = g_frameCallback;
        g_frameCallback = cpu.m_gpr[3];
        Utils::Log::error("[AX] frame callback registered: 0x{:08X}", g_frameCallback);
        cpu.m_gpr[3] = prev;
    }

    // Snapshot first so guest buffers may alias the voice's public offsets.
    void copyVoiceOffsets(Core::Interpreter &cpu, std::uint32_t source, std::uint32_t destination)
    {
        std::array<std::uint32_t, kVoiceOffsetsSize / 4> words{};
        for (std::size_t i = 0; i < words.size(); ++i)
            words[i] = cpu.m_memory.read<std::uint32_t>(source + i * 4);
        for (std::size_t i = 0; i < words.size(); ++i)
            cpu.m_memory.write<std::uint32_t>(destination + i * 4, words[i]);
    }

    void ax_GetVoiceOffsets(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3] && cpu.m_gpr[4])
            copyVoiceOffsets(cpu, cpu.m_gpr[3] + kVoiceOffsets, cpu.m_gpr[4]);
        cpu.m_gpr[3] = 0;
    }

    // AXSetVoiceOffsets(voice, const AXVoiceOffsets *): offsets relative to offsets->data.
    void ax_SetVoiceOffsets(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3] && cpu.m_gpr[4]) {
            copyVoiceOffsets(cpu, cpu.m_gpr[4], cpu.m_gpr[3] + kVoiceOffsets);
            if (auto *voice = findVoice(cpu.m_gpr[3])) {
                const auto source = cpu.m_gpr[4];
                voice->format = cpu.m_memory.read<std::uint16_t>(source);
                voice->loopFlag = cpu.m_memory.read<std::uint16_t>(source + 2);
                voice->base = cpu.m_memory.read<std::uint32_t>(source + 0x10);
                const auto origin = addressUnits(voice->base, voice->format);
                voice->loop = origin + cpu.m_memory.read<std::uint32_t>(source + 4);
                voice->end = origin + cpu.m_memory.read<std::uint32_t>(source + 8);
                voice->current = origin + cpu.m_memory.read<std::uint32_t>(source + 0xC);
                voice->fraction = 0;
            }
        }
        cpu.m_gpr[3] = 0;
    }

    // AXSetVoiceEndOffsetEx / AXSetVoiceLoopOffsetEx(voice, offset, samples)
    void ax_SetVoiceEndOffsetEx(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3])) {
            voice->base = cpu.m_gpr[5];
            voice->end = addressUnits(voice->base, voice->format) + cpu.m_gpr[4];
            writeVoiceOffsets(cpu, *voice);
        }
        cpu.m_gpr[3] = 0;
    }

    void ax_SetVoiceLoopOffsetEx(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3])) {
            voice->base = cpu.m_gpr[5];
            voice->loop = addressUnits(voice->base, voice->format) + cpu.m_gpr[4];
            writeVoiceOffsets(cpu, *voice);
        }
        cpu.m_gpr[3] = 0;
    }

    // AXGetVoiceCurrentOffsetEx(voice, samples) -> current offset relative to samples.
    void ax_GetVoiceCurrentOffsetEx(Core::Interpreter &cpu)
    {
        const auto *voice = findVoice(cpu.m_gpr[3]);
        cpu.m_gpr[3] = voice ? static_cast<std::uint32_t>(voice->current - addressUnits(cpu.m_gpr[4], voice->format)) : 0;
    }

    void ax_GetVoiceLoopCount(Core::Interpreter &cpu)
    {
        const auto *voice = findVoice(cpu.m_gpr[3]);
        cpu.m_gpr[3] = voice ? voice->loopCount : 0;
    }

    void ax_SetVoiceLoop(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3])) {
            voice->loopFlag = static_cast<std::uint16_t>(cpu.m_gpr[4]);
            writeVoiceOffsets(cpu, *voice);
        }
        cpu.m_gpr[3] = 0;
    }

    // AXSetVoiceState(voice, state): 0 = stopped, 1 = playing.
    void ax_SetVoiceState(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3])) {
            voice->state = cpu.m_gpr[4] & 0xFFFF;
            cpu.m_memory.write<std::uint32_t>(voice->guestPtr + kStateOffset, voice->state);
        }
        cpu.m_gpr[3] = 0;
    }

    // AXSetVoiceSrc(voice, const AXVoiceSrc *): 16.16 fixed-point ratio in the first two halves.
    void ax_SetVoiceSrc(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3]); voice && cpu.m_gpr[4]) {
            voice->ratio = std::min(64.0, cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[4]) / 65536.0);
            voice->fraction = cpu.m_memory.read<std::uint16_t>(cpu.m_gpr[4] + 4) / 65536.0;
        }
        cpu.m_gpr[3] = 0;
    }

    // AXSetVoiceSrcRatio(voice, float ratio) -> AX_RESULT
    void ax_SetVoiceSrcRatio(Core::Interpreter &cpu)
    {
        const double ratio = cpu.m_fpr[1];
        if (!std::isfinite(ratio) || ratio < 0 || ratio > 64) {
            cpu.m_gpr[3] = ratio < 0 ? std::uint32_t(-1) : std::uint32_t(-2);
            return;
        }
        if (auto *voice = findVoice(cpu.m_gpr[3]))
            voice->ratio = ratio;
        cpu.m_gpr[3] = 0;
    }

    // AXComputeLpfCoefs(u32 freq, u16* a0, u16* b0): passthrough filter.
    void ax_ComputeLpfCoefs(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint16_t>(cpu.m_gpr[4], 0x7FFF);
        if (cpu.m_gpr[5])
            cpu.m_memory.write<std::uint16_t>(cpu.m_gpr[5], 0);
        cpu.m_gpr[3] = 0;
    }

    void ax_GetAuxCallback(Core::Interpreter &cpu)
    {
        const auto type = cpu.m_gpr[3], id = cpu.m_gpr[4], bus = cpu.m_gpr[5];
        if (!validDevice(type, id) || bus >= 3) {
            cpu.m_gpr[3] = -1;
            return;
        }
        const auto &aux = g_aux[type][id][bus];
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], aux.callback);
        if (cpu.m_gpr[7])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[7], aux.userData);
        cpu.m_gpr[3] = 0;
    }
    void ax_RegisterAuxCallback(Core::Interpreter &cpu)
    {
        const auto type = cpu.m_gpr[3], id = cpu.m_gpr[4], bus = cpu.m_gpr[5];
        if (!validDevice(type, id) || bus >= 3) {
            cpu.m_gpr[3] = -1;
            return;
        }
        auto &aux = g_aux[type][id][bus];
        aux.callback = cpu.m_gpr[6];
        aux.userData = cpu.m_gpr[7];
        cpu.m_gpr[3] = 0;
    }
    void ax_SetAuxReturnVolume(Core::Interpreter &cpu)
    {
        const auto type = cpu.m_gpr[3], id = cpu.m_gpr[4], bus = cpu.m_gpr[5];
        if (!validDevice(type, id) || bus >= 3) {
            cpu.m_gpr[3] = -1;
            return;
        }
        g_aux[type][id][bus].volume = cpu.m_gpr[6];
        cpu.m_gpr[3] = 0;
    }
    void ax_SetDeviceVolume(Core::Interpreter &cpu)
    {
        const auto type = cpu.m_gpr[3], id = cpu.m_gpr[4];
        if (!validDevice(type, id)) {
            cpu.m_gpr[3] = -1;
            return;
        }
        g_deviceVolume[type][id] = cpu.m_gpr[5];
        cpu.m_gpr[3] = 0;
    }
    void ax_GetDeviceVolume(Core::Interpreter &cpu)
    {
        const auto type = cpu.m_gpr[3], id = cpu.m_gpr[4];
        if (!validDevice(type, id)) {
            cpu.m_gpr[3] = -1;
            return;
        }
        if (cpu.m_gpr[5])
            cpu.m_memory.write<std::uint16_t>(cpu.m_gpr[5], g_deviceVolume[type][id]);
        cpu.m_gpr[3] = 0;
    }
    void ax_SetDeviceUpsampleStage(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3] >= 2) {
            cpu.m_gpr[3] = -1;
            return;
        }
        const auto type = cpu.m_gpr[3];
        const bool after = cpu.m_gpr[4] != 0;
        if (g_upsampleAfter[type] != after)
            for (auto &converter: g_finalMix[type].upsamplers)
                converter.reset();
        g_upsampleAfter[type] = after;
        cpu.m_gpr[3] = 0;
    }
    void ax_GetDeviceUpsampleStage(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3] >= 2) {
            cpu.m_gpr[3] = -1;
            return;
        }
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], g_upsampleAfter[cpu.m_gpr[3]]);
        cpu.m_gpr[3] = 0;
    }
    void ax_GetDeviceMode(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3] >= 2) {
            cpu.m_gpr[3] = -1;
            return;
        }
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], g_deviceMode[cpu.m_gpr[3]]);
        cpu.m_gpr[3] = 0;
    }
    void ax_SetDeviceMode(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3] >= 2 || (cpu.m_gpr[4] > 3 && cpu.m_gpr[4] != 5)) {
            cpu.m_gpr[3] = -1;
            return;
        }
        g_deviceMode[cpu.m_gpr[3]] = cpu.m_gpr[4];
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoiceDeviceMix(Core::Interpreter &cpu)
    {
        auto *voice = findVoice(cpu.m_gpr[3]);
        const auto type = cpu.m_gpr[4], id = cpu.m_gpr[5], data = cpu.m_gpr[6];
        if (!voice || !validDevice(type, id) || !data) {
            cpu.m_gpr[3] = -1;
            return;
        }
        for (unsigned ch = 0; ch < channels(type); ++ch)
            for (unsigned bus = 0; bus < 4; ++bus) {
                auto &gain = voice->gains[type][id][ch][bus];
                gain.volume = cpu.m_memory.read<std::uint16_t>(data + ch * 16 + bus * 4);
                gain.delta = cpu.m_memory.read<std::int16_t>(data + ch * 16 + bus * 4 + 2);
            }
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoiceVe(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]); v && cpu.m_gpr[4]) {
            v->envelope = cpu.m_memory.read<std::uint16_t>(cpu.m_gpr[4]);
            v->envelopeDelta = cpu.m_memory.read<std::int16_t>(cpu.m_gpr[4] + 2);
            cpu.m_memory.write<std::uint32_t>(v->guestPtr + 8, v->envelope);
        }
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoiceVeDelta(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]))
            v->envelopeDelta = std::int16_t(cpu.m_gpr[4]);
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoiceAdpcm(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]); v && cpu.m_gpr[4]) {
            const auto data = cpu.m_gpr[4];
            for (unsigned i = 0; i < 16; ++i)
                v->coefficients[i] = cpu.m_memory.read<std::int16_t>(data + i * 2);
            v->predScale = cpu.m_memory.read<std::uint16_t>(data + 0x22);
            v->history1 = cpu.m_memory.read<std::int16_t>(data + 0x24);
            v->history2 = cpu.m_memory.read<std::int16_t>(data + 0x26);
        }
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoiceAdpcmLoop(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]); v && cpu.m_gpr[4]) {
            v->loopPredScale = cpu.m_memory.read<std::uint16_t>(cpu.m_gpr[4]);
            v->loopHistory1 = cpu.m_memory.read<std::int16_t>(cpu.m_gpr[4] + 2);
            v->loopHistory2 = cpu.m_memory.read<std::int16_t>(cpu.m_gpr[4] + 4);
        }
        cpu.m_gpr[3] = 0;
    }
    void ax_InitWithParams(Core::Interpreter &cpu)
    {
        const auto params = cpu.m_gpr[3];
        const bool rate48 = params && cpu.m_memory.read<std::uint32_t>(params) == 1;
        const bool initialized = g_initialized;
        ax_Init(cpu);
        if (!initialized)
            g_inputSamples = rate48 ? 144 : 96;
    }
    void ax_IsInit(Core::Interpreter &cpu) { cpu.m_gpr[3] = g_initialized; }
    void ax_GetInputSamplesPerFrame(Core::Interpreter &cpu) { cpu.m_gpr[3] = g_inputSamples; }
    void ax_GetInputSamplesPerSec(Core::Interpreter &cpu) { cpu.m_gpr[3] = g_inputSamples * 1000 / 3; }
    void ax_GetMaxVoices(Core::Interpreter &cpu) { cpu.m_gpr[3] = kMaxVoices; }
    void ax_IsVoiceRunning(Core::Interpreter &cpu)
    {
        auto *v = findVoice(cpu.m_gpr[3]);
        cpu.m_gpr[3] = v && v->state == 1;
    }
    void ax_SetVoiceSrcType(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]))
            v->srcType = cpu.m_gpr[4];
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoiceType(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]))
            v->type = cpu.m_gpr[4];
        cpu.m_gpr[3] = 0;
    }
    void ax_SetVoicePriority(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3]))
            cpu.m_memory.write<std::uint32_t>(v->guestPtr + 0x1C, cpu.m_gpr[4]);
        cpu.m_gpr[3] = 0;
    }
    template<int Field, bool Ex>
    void ax_SetOffset(Core::Interpreter &cpu)
    {
        if (auto *v = findVoice(cpu.m_gpr[3])) {
            if constexpr (Ex)
                v->base = cpu.m_gpr[5];
            const auto origin = addressUnits(v->base, v->format);
            if constexpr (Field == 0) {
                v->current = origin + cpu.m_gpr[4];
                v->fraction = 0;
            }
            if constexpr (Field == 1)
                v->end = origin + cpu.m_gpr[4];
            if constexpr (Field == 2)
                v->loop = origin + cpu.m_gpr[4];
            writeVoiceOffsets(cpu, *v);
        }
        cpu.m_gpr[3] = 0;
    }

    void writeVoiceOffsets(Core::Interpreter &cpu, const VoiceSlot &voice)
    {
        const auto origin = addressUnits(voice.base, voice.format);
        const auto offsets = voice.guestPtr + kVoiceOffsets;
        cpu.m_memory.write<std::uint16_t>(offsets + 2, voice.loopFlag);
        cpu.m_memory.write<std::uint32_t>(offsets + 16, voice.base);
        cpu.m_memory.write<std::uint32_t>(offsets + 4, static_cast<std::uint32_t>(voice.loop - origin));
        cpu.m_memory.write<std::uint32_t>(offsets + 8, static_cast<std::uint32_t>(voice.end - origin));
        cpu.m_memory.write<std::uint32_t>(offsets + 0xC, static_cast<std::uint32_t>(voice.current - origin));
    }

    const char *const kNoops[] = {
            "AXDecodeAdpcmData",
            "AXGetSwapProfile",
            "AXInitProfile",
            "AXRmtAdvancePtr",
            "AXRmtGetSamples",
            "AXRmtGetSamplesLeft",
            "AXSetDRCVSDownmixBalance",
            "AXSetDRCVSLC",
            "AXSetDRCVSMode",
            "AXSetDRCVSOutputGain",
            "AXSetDRCVSSpeakerPosition",
            "AXSetDRCVSSurroundDepth",
            "AXSetDRCVSSurroundLevelGain",
            "AXSetDefaultMixerSelect",
            "AXSetDeviceCompressor",
            "AXSetDeviceLinearUpsampler",
            "AXSetMaxVoices",
            "AXSetVoiceBiquad",
            "AXSetVoiceBiquadCoefs",
            "AXSetVoiceLpf",
            "AXSetVoiceLpfCoefs",
            "AXSetVoiceMixerSelect",
            "AXSetVoiceRmtIIR",
            "AXSetVoiceRmtIIRCoefs",
            "AXSetVoiceRmtOn",
            "AXUserBegin",
            "AXUserEnd",
            "AXUserIsProtected",
    };

} // namespace

void RegisterAxFunctions()
{
    g_voices.clear();
    g_appFrameCallbacks.clear();
    g_frameCallback = 0;
    g_initialized = false;
    g_finalMix = {};
    g_aux = {};
    g_deviceVolume = {{{0x8000, 0x8000}, {0x8000, 0x8000}}};
    g_upsampleAfter = {};
    g_deviceMode = {0, 0};
    g_buses = {};
    g_inputSamples = 96;
    g_phase = 3;
    Core::Audio::output().clear();
    g_axThread = nullptr;
    g_axStackTop = 0;
    g_pendingCbs.clear();
    g_pendingIdx = 0;
    g_nextFrameQuarterTicks = 0;
    g_frameCount = 0;
    g_firstFrameTick = 0;
    for (const char *name: kNoops)
        Core::syscallHandler.registerSyscall(name, ax_noop_ok);

    Core::syscallHandler.registerSyscall("AXInit", ax_Init);
    Core::syscallHandler.registerSyscall("AXQuit", ax_Quit);
    Core::syscallHandler.registerSyscall("AXAcquireVoice", ax_AcquireVoice);
    Core::syscallHandler.registerSyscall("AXAcquireVoiceEx", ax_AcquireVoiceEx);
    Core::syscallHandler.registerSyscall("AXFreeVoice", ax_FreeVoice);
    Core::syscallHandler.registerSyscall("AXRegisterAppFrameCallback", ax_RegisterAppFrameCallback);
    Core::syscallHandler.registerSyscall("AXDeregisterAppFrameCallback", ax_DeregisterAppFrameCallback);
    Core::syscallHandler.registerSyscall("AXRegisterFrameCallback", ax_RegisterFrameCallback);
    Core::syscallHandler.registerSyscall("AXGetVoiceOffsets", ax_GetVoiceOffsets);
    Core::syscallHandler.registerSyscall("AXSetVoiceOffsets", ax_SetVoiceOffsets);
    Core::syscallHandler.registerSyscall("AXSetVoiceEndOffsetEx", ax_SetVoiceEndOffsetEx);
    Core::syscallHandler.registerSyscall("AXSetVoiceLoopOffsetEx", ax_SetVoiceLoopOffsetEx);
    Core::syscallHandler.registerSyscall("AXGetVoiceCurrentOffsetEx", ax_GetVoiceCurrentOffsetEx);
    Core::syscallHandler.registerSyscall("AXGetVoiceLoopCount", ax_GetVoiceLoopCount);
    Core::syscallHandler.registerSyscall("AXSetVoiceLoop", ax_SetVoiceLoop);
    Core::syscallHandler.registerSyscall("AXSetVoiceState", ax_SetVoiceState);
    Core::syscallHandler.registerSyscall("AXSetVoiceSrc", ax_SetVoiceSrc);
    Core::syscallHandler.registerSyscall("AXSetVoiceSrcRatio", ax_SetVoiceSrcRatio);
    Core::syscallHandler.registerSyscall("AXComputeLpfCoefs", ax_ComputeLpfCoefs);
    Core::syscallHandler.registerSyscall("AXGetAuxCallback", ax_GetAuxCallback);
    Core::syscallHandler.registerSyscall("AXRegisterDeviceFinalMixCallback", ax_RegisterDeviceFinalMixCallback);
    Core::syscallHandler.registerSyscall("AXGetDeviceFinalMixCallback", ax_GetDeviceFinalMixCallback);
    Core::syscallHandler.registerSyscall("AXInitWithParams", ax_InitWithParams);
    Core::syscallHandler.registerSyscall("AXIsInit", ax_IsInit);
    Core::syscallHandler.registerSyscall("AXGetInputSamplesPerFrame", ax_GetInputSamplesPerFrame);
    Core::syscallHandler.registerSyscall("AXGetInputSamplesPerSec", ax_GetInputSamplesPerSec);
    Core::syscallHandler.registerSyscall("AXGetMaxVoices", ax_GetMaxVoices);
    Core::syscallHandler.registerSyscall("AXIsVoiceRunning", ax_IsVoiceRunning);
    Core::syscallHandler.registerSyscall("AXRegisterAuxCallback", ax_RegisterAuxCallback);
    Core::syscallHandler.registerSyscall("AXSetAuxReturnVolume", ax_SetAuxReturnVolume);
    Core::syscallHandler.registerSyscall("AXSetDeviceVolume", ax_SetDeviceVolume);
    Core::syscallHandler.registerSyscall("AXGetDeviceVolume", ax_GetDeviceVolume);
    Core::syscallHandler.registerSyscall("AXGetDeviceMode", ax_GetDeviceMode);
    Core::syscallHandler.registerSyscall("AXSetDeviceMode", ax_SetDeviceMode);
    Core::syscallHandler.registerSyscall("AXGetDeviceUpsampleStage", ax_GetDeviceUpsampleStage);
    Core::syscallHandler.registerSyscall("AXSetDeviceUpsampleStage", ax_SetDeviceUpsampleStage);
    Core::syscallHandler.registerSyscall("AXSetVoiceDeviceMix", ax_SetVoiceDeviceMix);
    Core::syscallHandler.registerSyscall("AXSetVoiceVe", ax_SetVoiceVe);
    Core::syscallHandler.registerSyscall("AXSetVoiceVeDelta", ax_SetVoiceVeDelta);
    Core::syscallHandler.registerSyscall("AXSetVoiceAdpcm", ax_SetVoiceAdpcm);
    Core::syscallHandler.registerSyscall("AXSetVoiceAdpcmLoop", ax_SetVoiceAdpcmLoop);
    Core::syscallHandler.registerSyscall("AXSetVoiceSrcType", ax_SetVoiceSrcType);
    Core::syscallHandler.registerSyscall("AXSetVoiceType", ax_SetVoiceType);
    Core::syscallHandler.registerSyscall("AXSetVoicePriority", ax_SetVoicePriority);
    Core::syscallHandler.registerSyscall("AXSetVoiceCurrentOffset", ax_SetOffset<0, false>);
    Core::syscallHandler.registerSyscall("AXSetVoiceCurrentOffsetEx", ax_SetOffset<0, true>);
    Core::syscallHandler.registerSyscall("AXSetVoiceEndOffset", ax_SetOffset<1, false>);
    Core::syscallHandler.registerSyscall("AXSetVoiceLoopOffset", ax_SetOffset<2, false>);
    Core::syscallHandler.registerSyscall("AXSetVoiceSamplesAddr", ax_SetVoiceSamplesAddr);
    Core::syscallHandler.registerSyscall("AXVoiceIsProtected", ax_noop_ok);
    Core::syscallHandler.registerSyscall("AXPrepareEfxData", ax_noop_ok);
    Core::syscallHandler.registerSyscall("AXVoiceBegin", ax_noop_ok);
    Core::syscallHandler.registerSyscall("AXVoiceEnd", ax_noop_ok);
}
