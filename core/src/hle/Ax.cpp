/*
** EPITECH PROJECT, 2026
** core
** File description:
** Ax -- minimal sndcore2 (AX) HLE. No audio is rendered yet; the goal is structural
** correctness: AXAcquireVoice hands out real zeroed guest-memory voice structs (titles deref
** them immediately), setters accept everything, queries return silence/defaults, and frame
** callbacks are remembered so a later audio pump can fire them.
*/

#include "Ax.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/Scheduler.hpp"
#include "utils/Logger.hpp"

namespace {

    constexpr std::uint32_t kMaxVoices = 96;
    constexpr std::uint32_t kVoiceSize = 0x200; // generous; real AXVoice is ~0x60 + padding
    constexpr std::uint32_t kVoiceOffsets = 0x34;
    constexpr std::uint32_t kVoiceOffsetsSize = 0x14;

    // Playback model: no samples are mixed, but a playing voice consumes its source at the
    // SRC ratio each 3 ms frame, loops or stops at its end offset, and reports its position.
    // Streamed sound players poll that position (and wait for clips to finish), so a voice
    // that never advances stalls anything synchronised to audio.
    // Offsets are kept absolute in AX address units: nibbles for ADPCM, samples for PCM.
    struct VoiceSlot {
            std::uint32_t guestPtr{0};
            bool used{false};
            std::uint32_t state{0};
            std::uint16_t format{0}, loopFlag{0};
            std::uint32_t base{0}; // samples pointer the public offsets are relative to
            std::uint64_t loop{0}, end{0}, current{0};
            std::uint32_t loopCount{0};
            double ratio{1.0}, fraction{0.0};
    };
    std::vector<VoiceSlot> g_voices;

    constexpr std::uint16_t kFormatAdpcm = 0x00, kFormatPcm16 = 0x0A;
    constexpr std::uint32_t kStateOffset = 0x04;

    std::uint64_t addressUnits(std::uint32_t address, std::uint16_t format)
    {
        return format == kFormatAdpcm ? std::uint64_t(address) * 2 : format == kFormatPcm16 ? address / 2 : address;
    }

    // ADPCM frames are 8 bytes: two header nibbles, then 14 sample nibbles.
    std::uint64_t unitsToSample(std::uint64_t units, std::uint16_t format)
    {
        if (format != kFormatAdpcm)
            return units;
        const auto inFrame = units % 16;
        return units / 16 * 14 + (inFrame < 2 ? 0 : inFrame - 2);
    }

    std::uint64_t sampleToUnits(std::uint64_t sample, std::uint16_t format)
    {
        return format == kFormatAdpcm ? sample / 14 * 16 + sample % 14 + 2 : sample;
    }

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
            std::uint32_t argument = 0;
    };
    std::vector<PendingCallback> g_pendingCbs;
    std::size_t g_pendingIdx = 0;

    bool hasCallbacks() { return g_frameCallback || !g_appFrameCallbacks.empty() || g_finalMix[0].callback || g_finalMix[1].callback; }

    void advanceVoices(Core::Interpreter &cpu)
    {
        constexpr double kOutputSamples = 144.0; // 3 ms at 48 kHz
        for (auto &voice: g_voices) {
            if (!voice.used || voice.state != 1 || !voice.base)
                continue;
            const double consumed = kOutputSamples * voice.ratio + voice.fraction;
            const auto whole = static_cast<std::uint64_t>(consumed);
            voice.fraction = consumed - double(whole);
            auto position = unitsToSample(voice.current, voice.format) + whole;
            const auto endSample = unitsToSample(voice.end, voice.format);
            if (position > endSample) {
                const auto loopSample = unitsToSample(voice.loop, voice.format);
                if (voice.loopFlag && loopSample <= endSample) {
                    const auto length = endSample - loopSample + 1;
                    const auto overrun = position - endSample - 1;
                    voice.loopCount += static_cast<std::uint32_t>(1 + overrun / length);
                    position = loopSample + overrun % length;
                } else {
                    position = endSample;
                    voice.state = 0;
                    cpu.m_memory.write<std::uint32_t>(voice.guestPtr + kStateOffset, 0);
                }
            }
            voice.current = sampleToUnits(position, voice.format);
            writeVoiceOffsets(cpu, voice);
        }
    }

    void buildPending(Core::Interpreter &cpu)
    {
        if (!g_frameCount)
            g_firstFrameTick = cpu.m_scheduler.now();
        ++g_frameCount;
        if (std::getenv("WEMU_AX_TRACE") && (g_frameCount <= 8 || g_frameCount % 256 == 0))
            Utils::Log::error("[AX_CLOCK] frame={} tick={} elapsed_ticks={} previous_deadline_quarters={}", g_frameCount, cpu.m_scheduler.now(),
                              cpu.m_scheduler.now() - g_firstFrameTick, g_nextFrameQuarterTicks);
        g_nextFrameQuarterTicks += kFrameQuarterTicks;
        advanceVoices(cpu);
        g_pendingCbs.clear();
        g_pendingIdx = 0;
        if (g_frameCallback)
            g_pendingCbs.push_back({g_frameCallback});
        for (auto callback: g_appFrameCallbacks)
            g_pendingCbs.push_back({callback});
        for (std::size_t device = 0; device < g_finalMix.size(); ++device) {
            auto &mix = g_finalMix[device];
            if (!mix.callback)
                continue;
            // Default final mix is after upsampling: 3 ms of planar signed 32-bit
            // samples at 48 kHz. Voice mixing is not implemented; input is silence.
            const std::uint16_t channels = device == 0 ? 6 : 4;
            const std::uint16_t devices = device == 0 ? 1 : 2;
            constexpr std::uint16_t samples = 144;
            const auto planes = channels * devices;
            const auto bytes = 12 + planes * 4 + planes * samples * 4;
            if (!mix.storage)
                mix.storage = cpu.m_memory.heapAllocate(bytes, 0x20);
            for (std::uint32_t off = 0; off < bytes; off += 4)
                cpu.m_memory.write<std::uint32_t>(mix.storage + off, 0);
            const auto pointers = mix.storage + 12;
            const auto data = pointers + planes * 4;
            cpu.m_memory.write<std::uint32_t>(mix.storage, pointers);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 4, channels);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 6, samples);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 8, devices);
            cpu.m_memory.write<std::uint16_t>(mix.storage + 10, channels);
            for (int plane = 0; plane < planes; ++plane)
                cpu.m_memory.write<std::uint32_t>(pointers + plane * 4, data + plane * samples * 4);
            g_pendingCbs.push_back({mix.callback, mix.storage});
        }
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

    void OnFrameTick(Core::Interpreter &cpu)
    {
        if (!g_initialized || cpu.m_scheduler.now() * 4 < g_nextFrameQuarterTicks)
            return;
        if (!hasCallbacks())
            return;
        ensureAxThread(cpu);
        if (g_axThread->state != Core::ThreadContext::State::Paused)
            return; // previous frame still in flight; skip this one
        buildPending(cpu);
        Core::ThreadContext *t = g_axThread;
        const auto callback = g_pendingCbs[g_pendingIdx++];
        t->pc = callback.function - Core::Memory::MemoryMap::ApplicationCode;
        t->gpr[3] = callback.argument;
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
        if (g_pendingIdx < g_pendingCbs.size()) { // next callback of this frame
            const auto callback = g_pendingCbs[g_pendingIdx++];
            cpu.m_pc = callback.function - appCode;
            cpu.m_gpr[3] = callback.argument;
            cpu.m_nextPc = cpu.m_pc;
            cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
            cpu.m_gpr[1] = (g_axStackTop & ~0xFu) - 8;
            return;
        }
        g_pendingCbs.clear();
        g_pendingIdx = 0;
        if (!g_initialized || !hasCallbacks()) {
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
        if (!g_pendingCbs.empty()) {
            const auto callback = g_pendingCbs[g_pendingIdx++];
            cpu.m_pc = callback.function - appCode;
            cpu.m_gpr[3] = callback.argument;
            cpu.m_nextPc = cpu.m_pc;
            cpu.m_lr = Core::AX_FRAME_SENTINEL - appCode;
            cpu.m_gpr[1] = (g_axStackTop & ~0xFu) - 8;
            return;
        }
        cpu.m_scheduler.sleep(cpu, 1000);
    }

} // namespace Core::Ax

namespace {

    void ax_noop_ok(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    void ax_Init(Core::Interpreter &cpu)
    {
        g_initialized = true;
        g_nextFrameQuarterTicks = cpu.m_scheduler.now() * 4 + kFrameQuarterTicks;
        cpu.m_gpr[3] = 0;
    }

    void ax_Quit(Core::Interpreter &cpu)
    {
        g_initialized = false;
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
            cpu.m_memory.write<std::uint32_t>(slot.guestPtr + 0x08, cpu.m_gpr[3]); // priority
            slot = {slot.guestPtr, true};
            cpu.m_gpr[3] = slot.guestPtr;
            return;
        }
        Utils::Log::error("[AX] out of voices");
        cpu.m_gpr[3] = 0;
    }

    void ax_FreeVoice(Core::Interpreter &cpu)
    {
        for (auto &slot: g_voices)
            if (slot.guestPtr == cpu.m_gpr[3])
                slot.used = false;
        cpu.m_gpr[3] = 0;
    }

    // AXRegisterAppFrameCallback(fn) -> 0; fired each frame by the pump.
    void ax_RegisterAppFrameCallback(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3]) {
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
            voice->end = addressUnits(cpu.m_gpr[5], voice->format) + cpu.m_gpr[4];
            writeVoiceOffsets(cpu, *voice);
        }
        cpu.m_gpr[3] = 0;
    }

    void ax_SetVoiceLoopOffsetEx(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3])) {
            voice->loop = addressUnits(cpu.m_gpr[5], voice->format) + cpu.m_gpr[4];
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
        if (auto *voice = findVoice(cpu.m_gpr[3]); voice && cpu.m_gpr[4])
            voice->ratio = cpu.m_memory.read<std::uint16_t>(cpu.m_gpr[4]) + cpu.m_memory.read<std::uint16_t>(cpu.m_gpr[4] + 2) / 65536.0;
        cpu.m_gpr[3] = 0;
    }

    // AXSetVoiceSrcRatio(voice, float ratio) -> AX_RESULT
    void ax_SetVoiceSrcRatio(Core::Interpreter &cpu)
    {
        if (auto *voice = findVoice(cpu.m_gpr[3]); voice && cpu.m_fpr[1] >= 0 && cpu.m_fpr[1] < 64)
            voice->ratio = cpu.m_fpr[1];
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

    // AXGetAuxCallback(dev, unk, aux, fn** outCb, void** outData): none registered.
    void ax_GetAuxCallback(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], 0);
        if (cpu.m_gpr[7])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[7], 0);
        cpu.m_gpr[3] = 0;
    }

    void writeVoiceOffsets(Core::Interpreter &cpu, const VoiceSlot &voice)
    {
        const auto origin = addressUnits(voice.base, voice.format);
        const auto offsets = voice.guestPtr + kVoiceOffsets;
        cpu.m_memory.write<std::uint16_t>(offsets + 2, voice.loopFlag);
        cpu.m_memory.write<std::uint32_t>(offsets + 4, static_cast<std::uint32_t>(voice.loop - origin));
        cpu.m_memory.write<std::uint32_t>(offsets + 8, static_cast<std::uint32_t>(voice.end - origin));
        cpu.m_memory.write<std::uint32_t>(offsets + 0xC, static_cast<std::uint32_t>(voice.current - origin));
    }

    const char *const kNoops[] = {
            "AXDecodeAdpcmData",
            "AXGetDeviceMode",
            "AXGetDeviceUpsampleStage",
            "AXGetSwapProfile",
            "AXInitProfile",
            "AXRegisterAuxCallback",
            "AXRmtAdvancePtr",
            "AXRmtGetSamples",
            "AXRmtGetSamplesLeft",
            "AXSetAuxReturnVolume",
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
            "AXSetDeviceUpsampleStage",
            "AXSetDeviceVolume",
            "AXSetMaxVoices",
            "AXSetVoiceAdpcm",
            "AXSetVoiceAdpcmLoop",
            "AXSetVoiceBiquad",
            "AXSetVoiceBiquadCoefs",
            "AXSetVoiceDeviceMix",
            "AXSetVoiceLpf",
            "AXSetVoiceLpfCoefs",
            "AXSetVoiceMixerSelect",
            "AXSetVoicePriority",
            "AXSetVoiceRmtIIR",
            "AXSetVoiceRmtIIRCoefs",
            "AXSetVoiceRmtOn",
            "AXSetVoiceSrcType",
            "AXSetVoiceType",
            "AXSetVoiceVe",
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
    Core::syscallHandler.registerSyscall("AXAcquireVoiceEx", ax_AcquireVoice);
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
}
