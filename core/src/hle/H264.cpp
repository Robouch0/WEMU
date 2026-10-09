#include "H264.hpp"

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "video/H264Decoder.hpp"
#include "utils/Logger.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <unordered_map>

namespace Core::H264 {
    struct Session {
        std::unique_ptr<Video::H264Decoder> decoder;
        std::deque<Video::DecodedFrame> pending;
        std::uint32_t callback{}, user{}, bitstream{}, length{}, submitted{};
        double timestamp{};
        bool begun{}, outputPerFrame{}, firstBegin{true}, recovering{};
        std::uint32_t outputCount{};
        std::uint32_t recoveryDrops{};
        std::chrono::steady_clock::time_point firstOutputTime{};
    };
    struct CallbackReturn { std::uint32_t stack, lr, result; };
    struct State {
        std::unordered_map<std::uint32_t, Session> sessions;
        std::unordered_map<std::uint32_t, std::vector<CallbackReturn>> callbacks;
    };

    void onCallbackReturn(Interpreter &cpu)
    {
        if (!cpu.m_h264)
            throw std::runtime_error("H264 callback return without state");
        auto &calls = cpu.m_h264->callbacks[cpu.m_scheduler.currentHandle()];
        if (calls.empty())
            throw std::runtime_error("H264 callback return without caller");
        const auto caller = calls.back();
        calls.pop_back();
        cpu.m_gpr[1] = caller.stack;
        cpu.m_gpr[3] = caller.result;
        cpu.m_lr = caller.lr;
        cpu.m_pc = caller.lr;
        cpu.m_nextPc = caller.lr;
    }
}

namespace {
    constexpr std::uint32_t badStream = 0x1000000;
    constexpr std::uint32_t invalidParameter = 0x1010000;
    constexpr std::uint32_t invalidProfile = 0x1080000;

    Core::H264::State &state(Core::Interpreter &cpu)
    {
        if (!cpu.m_h264) cpu.m_h264 = std::make_shared<Core::H264::State>();
        return *cpu.m_h264;
    }

    std::uint8_t *range(Core::Interpreter &cpu, std::uint32_t address, std::size_t length)
    {
        if (!address || !length || length > UINT32_MAX || std::uint64_t(address) + length > UINT32_MAX)
            return nullptr;
        auto *start = cpu.m_memory.hostPtr(address);
        auto *end = cpu.m_memory.hostPtr(address + static_cast<std::uint32_t>(length) - 1);
        return start && end && reinterpret_cast<std::uintptr_t>(end) - reinterpret_cast<std::uintptr_t>(start) == length - 1
            ? start : nullptr;
    }

    Core::H264::Session *session(Core::Interpreter &cpu)
    {
        auto &sessions = state(cpu).sessions;
        auto it = sessions.find(cpu.m_gpr[3]);
        return it == sessions.end() ? nullptr : &it->second;
    }

    template<auto Function> void guarded(Core::Interpreter &cpu)
    {
        try {
            const auto result = Function(cpu);
            if (!cpu.m_hle_redirected) cpu.m_gpr[3] = result;
        } catch (const std::exception &error) {
            Utils::Log::error("[H264] {}", error.what());
            cpu.m_gpr[3] = badStream;
        }
    }

    std::uint32_t checkMemory(Core::Interpreter &cpu)
    {
        return range(cpu, cpu.m_gpr[3], cpu.m_gpr[4]) ? 0 : 1;
    }

    std::uint32_t init(Core::Interpreter &cpu)
    {
        const auto size = cpu.m_gpr[3], memory = cpu.m_gpr[4];
        if (size < 256 || !range(cpu, memory, size)) return invalidParameter;
        state(cpu).sessions[memory] = Core::H264::Session{};
        std::memset(range(cpu, memory, 256), 0, 256);
        return 0;
    }

    std::uint32_t open(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s || s->decoder) return invalidParameter;
        s->decoder = std::make_unique<Core::Video::H264Decoder>();
        return 0;
    }

    std::uint32_t begin(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s || !s->decoder || s->begun) return invalidParameter;
        s->decoder->reset();
        s->pending.clear();
        s->submitted = 0;
        s->length = 0;
        s->recovering = !s->firstBegin;
        s->recoveryDrops = 0;
        s->firstBegin = false;
        s->begun = true;
        if (std::getenv("WEMU_H264_TRACE"))
            Utils::Log::error("[H264] begin session=0x{:08X} recovering={} delivered={} tick={} lr=0x{:08X}",
                cpu.m_gpr[3], s->recovering, s->outputCount, cpu.m_scheduler.now(),
                cpu.m_lr + Core::Memory::ApplicationCode);
        return 0;
    }

    std::uint32_t closeDecoder(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s) return invalidParameter;
        if (std::getenv("WEMU_H264_TRACE"))
            Utils::Log::error("[H264] close session=0x{:08X} submitted={} pending={} delivered={}",
                cpu.m_gpr[3], s->submitted, s->pending.size(), s->outputCount);
        s->decoder.reset();
        s->pending.clear();
        s->begun = false;
        s->firstBegin = true;
        s->recovering = false;
        s->length = 0;
        return 0;
    }

    template<unsigned Kind> std::uint32_t setParameter(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s) return invalidParameter;
        const auto value = cpu.m_gpr[4];
        if constexpr (Kind == 1) s->callback = value;
        if constexpr (Kind == 2) s->outputPerFrame = value != 0;
        if constexpr (Kind == 3) {
            if (!range(cpu, value, 4)) return invalidParameter;
            s->user = cpu.m_memory.read<std::uint32_t>(value);
        }
        return 0;
    }

    std::uint32_t setGenericParameter(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s) return invalidParameter;
        const auto kind = cpu.m_gpr[4], value = cpu.m_gpr[5];
        if (kind == 1) s->callback = value;
        else if (kind == 0x70000001) s->user = value;
        else if (kind == 0x20000002) {
            if (!range(cpu, value, 1)) return invalidParameter;
            s->outputPerFrame = *range(cpu, value, 1) != 0;
        } else return invalidParameter;
        return 0;
    }

    std::uint32_t setBitstream(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s || !s->begun || !range(cpu, cpu.m_gpr[4], cpu.m_gpr[5])) return invalidParameter;
        s->bitstream = cpu.m_gpr[4];
        s->length = cpu.m_gpr[5];
        s->timestamp = cpu.m_fpr[1];
        return 0;
    }

    std::uint32_t output(Core::Interpreter &cpu, Core::H264::Session &s, std::size_t count, std::uint32_t result)
    {
        if (!count) return result;
        if (count > 32 || count > s.pending.size()) return badStream;
        const auto oldStack = cpu.m_gpr[1];
        const auto bytes = static_cast<std::uint32_t>((128 + count * 0x80 + 15) & ~15u);
        if (oldStack < bytes) return invalidParameter;
        const auto stack = (oldStack - bytes) & ~15u;
        if (s.callback && (!range(cpu, stack, bytes) || !range(cpu, s.callback, 4))) return invalidParameter;
        for (std::size_t i = 0; i < count; ++i)
            if (!range(cpu, s.pending[i].guestBuffer, s.pending[i].nv12.size())) return invalidParameter;

        // Leave the linkage and argument-save area available to the guest callee.
        const auto descriptor = stack + 64;
        const auto pointers = stack + 80;
        const auto records = pointers + static_cast<std::uint32_t>(count * 4 + 7) / 8 * 8;
        if (s.callback) {
            std::memset(range(cpu, stack, bytes), 0, bytes);
            cpu.m_memory.write<std::uint32_t>(stack, oldStack);
            cpu.m_memory.write<std::uint32_t>(descriptor, count);
            cpu.m_memory.write<std::uint32_t>(descriptor + 4, pointers);
            cpu.m_memory.write<std::uint32_t>(descriptor + 8, s.user);
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto &frame = s.pending.front();
            if (!s.outputCount) s.firstOutputTime = std::chrono::steady_clock::now();
            if (std::getenv("WEMU_H264_TRACE") && (s.outputCount < 8 || s.outputCount % 60 == 0)) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - s.firstOutputTime).count();
                std::uint8_t low = 255, high = 0;
                for (std::uint32_t row = 0; row < frame.height; ++row) {
                    const auto first = frame.nv12.begin() + row * frame.stride;
                    const auto bounds = std::minmax_element(first, first + frame.width);
                    low = std::min(low, *bounds.first);
                    high = std::max(high, *bounds.second);
                }
                Utils::Log::error("[H264] output #{} {}x{} stride={} timestamp={} buffer=0x{:08X} callback=0x{:08X} luma={}..{} wall_ms={} tick={}",
                    s.outputCount, frame.width, frame.height, frame.stride, frame.timestamp, frame.guestBuffer, s.callback, low, high, elapsed,
                    cpu.m_scheduler.now());
            }
            ++s.outputCount;
            std::memcpy(range(cpu, frame.guestBuffer, frame.nv12.size()), frame.nv12.data(), frame.nv12.size());
            if (s.callback) {
                const auto record = records + static_cast<std::uint32_t>(i * 0x78);
                cpu.m_memory.write<std::uint32_t>(pointers + i * 4, record);
                cpu.m_memory.write<std::uint32_t>(record, 100);
                cpu.m_memory.write<std::uint64_t>(record + 8, std::bit_cast<std::uint64_t>(frame.timestamp));
                cpu.m_memory.write<std::uint32_t>(record + 0x10, frame.width);
                cpu.m_memory.write<std::uint32_t>(record + 0x14, frame.height);
                cpu.m_memory.write<std::uint32_t>(record + 0x18, frame.stride);
                cpu.m_memory.write<std::uint8_t>(record + 0x1C,
                    (frame.cropTop || frame.cropBottom || frame.cropLeft || frame.cropRight) ? 1 : 0);
                cpu.m_memory.write<std::uint32_t>(record + 0x20, frame.cropTop);
                cpu.m_memory.write<std::uint32_t>(record + 0x24, frame.cropBottom);
                cpu.m_memory.write<std::uint32_t>(record + 0x28, frame.cropLeft);
                cpu.m_memory.write<std::uint32_t>(record + 0x2C, frame.cropRight);
                cpu.m_memory.write<std::uint32_t>(record + 0x44, frame.guestBuffer);
            }
            s.pending.pop_front();
        }
        if (s.callback) {
            state(cpu).callbacks[cpu.m_scheduler.currentHandle()].push_back({oldStack, cpu.m_lr, result});
            cpu.m_gpr[1] = stack;
            cpu.m_gpr[3] = descriptor;
            cpu.m_lr = Core::H264::callbackSentinel - Core::Memory::ApplicationCode;
            cpu.m_nextPc = s.callback - Core::Memory::ApplicationCode;
            cpu.m_hle_redirected = true;
        }
        return result;
    }

    std::uint32_t execute(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s || !s->begun || !s->length || !range(cpu, cpu.m_gpr[4], 1)) return invalidParameter;
        const auto *input = range(cpu, s->bitstream, s->length);
        if (!input) return invalidParameter;
        if (s->recovering) {
            bool idr = false;
            for (std::size_t i = 0; i + 3 < s->length; ++i) {
                if (input[i] || input[i + 1] || input[i + 2] != 1) continue;
                const auto type = input[i + 3] & 31;
                if (type == 1 || type == 5) { idr = type == 5; break; }
            }
            if (!idr) {
                ++s->recoveryDrops;
                if (std::getenv("WEMU_H264_TRACE") && (s->recoveryDrops < 8 || s->recoveryDrops % 60 == 0))
                    Utils::Log::error("[H264] recovery drop #{} session=0x{:08X} timestamp={}",
                        s->recoveryDrops, cpu.m_gpr[3], s->timestamp);
                s->length = 0;
                return 0x400;
            }
            s->recovering = false;
        }
        const bool trace = std::getenv("WEMU_H264_TRACE") && (s->submitted < 8 || s->submitted % 60 == 0);
        const auto started = trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        auto frames = s->decoder->decode({input, s->length}, s->timestamp, cpu.m_gpr[4]);
        if (trace)
            Utils::Log::error("[H264] submit #{} session=0x{:08X} timestamp={} bytes={} decoded={} pending={} decode_us={}",
                s->submitted, cpu.m_gpr[3], s->timestamp, s->length, frames.size(), s->pending.size(),
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        s->length = 0;
        ++s->submitted;
        for (auto &frame : frames) s->pending.push_back(std::move(frame));
        const auto count = s->outputPerFrame ? s->pending.size() : (s->submitted > 5 && !s->pending.empty() ? 1 : 0);
        return output(cpu, *s, count, 0xE4);
    }

    template<bool End> std::uint32_t flush(Core::Interpreter &cpu)
    {
        auto *s = session(cpu);
        if (!s || !s->decoder || !s->begun) return invalidParameter;
        auto frames = s->decoder->drain();
        if (std::getenv("WEMU_H264_TRACE"))
            Utils::Log::error("[H264] flush end={} session=0x{:08X} submitted={} decoded={} pending={} delivered={} tick={} lr=0x{:08X}",
                End, cpu.m_gpr[3], s->submitted, frames.size(), s->pending.size(), s->outputCount,
                cpu.m_scheduler.now(), cpu.m_lr + Core::Memory::ApplicationCode);
        for (auto &frame : frames) s->pending.push_back(std::move(frame));
        s->length = 0;
        s->submitted = 0;
        s->begun = !End;
        s->decoder->reset();
        return output(cpu, *s, s->pending.size(), 0);
    }

    std::uint32_t getImageSize(Core::Interpreter &cpu)
    {
        const auto source = cpu.m_gpr[3], size = cpu.m_gpr[4], offset = cpu.m_gpr[5];
        if (offset >= size || !range(cpu, source, size) || !range(cpu, cpu.m_gpr[6], 4)
            || !range(cpu, cpu.m_gpr[7], 4)) return invalidParameter;
        std::uint32_t width{}, height{};
        if (!Core::Video::H264Decoder::imageSize({range(cpu, source + offset, size - offset), size - offset}, width, height))
            return badStream;
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], width);
        cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[7], height);
        return 0;
    }

    void memoryRequirement(Core::Interpreter &cpu)
    {
        const auto profile = cpu.m_gpr[3];
        const auto level = cpu.m_gpr[4];
        const auto width = cpu.m_gpr[5];
        const auto height = cpu.m_gpr[6];
        const auto output = cpu.m_gpr[7];
        if (!output || width < 32 || height < 32 || width > 2800 || height > 1408 || level >= 52) {
            cpu.m_gpr[3] = invalidParameter;
            return;
        }
        if (profile != 66 && profile != 77 && profile != 100) {
            cpu.m_gpr[3] = invalidProfile;
            return;
        }
        // Cafe work-buffer sizes are determined by the level's decoded-picture-buffer limit,
        // not by a title or by the host decoder's allocation requirements.
        struct LevelLimit { std::uint32_t lastLevel, bytes; };
        constexpr std::array limits{
            LevelLimit{10, 0x63000}, LevelLimit{11, 0xE1000},
            LevelLimit{20, 0x252000}, LevelLimit{21, 0x4A4000},
            LevelLimit{30, 0x7E9000}, LevelLimit{31, 0x1194000},
            LevelLimit{32, 0x1400000}, LevelLimit{41, 0x2000000},
            LevelLimit{42, 0x2200000}, LevelLimit{50, 0x6BD0000},
            LevelLimit{51, 0xB400000}
        };
        for (const auto &limit : limits) {
            if (level <= limit.lastLevel) {
                cpu.m_memory.write<std::uint32_t>(output, limit.bytes + 0x447);
                cpu.m_gpr[3] = 0;
                return;
            }
        }
    }
}

void RegisterH264Functions()
{
    Core::syscallHandler.registerSyscall("H264DECMemoryRequirement", memoryRequirement);
    Core::syscallHandler.registerSyscall("H264DECCheckMemSegmentation", guarded<checkMemory>);
    Core::syscallHandler.registerSyscall("H264DECInitParam", guarded<init>);
    Core::syscallHandler.registerSyscall("H264DECOpen", guarded<open>);
    Core::syscallHandler.registerSyscall("H264DECBegin", guarded<begin>);
    Core::syscallHandler.registerSyscall("H264DECClose", guarded<closeDecoder>);
    Core::syscallHandler.registerSyscall("H264DECSetParam_FPTR_OUTPUT", guarded<setParameter<1>>);
    Core::syscallHandler.registerSyscall("H264DECSetParam_OUTPUT_PER_FRAME", guarded<setParameter<2>>);
    Core::syscallHandler.registerSyscall("H264DECSetParam_USER_MEMORY", guarded<setParameter<3>>);
    Core::syscallHandler.registerSyscall("H264DECSetParam", guarded<setGenericParameter>);
    Core::syscallHandler.registerSyscall("H264DECSetBitstream", guarded<setBitstream>);
    Core::syscallHandler.registerSyscall("H264DECExecute", guarded<execute>);
    Core::syscallHandler.registerSyscall("H264DECFlush", guarded<flush<false>>);
    Core::syscallHandler.registerSyscall("H264DECEnd", guarded<flush<true>>);
    Core::syscallHandler.registerSyscall("H264DECGetImageSize", guarded<getImageSize>);
}
