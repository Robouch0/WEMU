#include "hle/MvPlayer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "cpu/interpreter/Interpreter.hpp"
#include "gfx/Gx2CommandStream.hpp"
#include "gfx/Renderer.hpp"
#include "hle/Fs.hpp"
#include "utils/Logger.hpp"
#include "video/MovieDecoder.hpp"

namespace Core::MvPlayer {
    namespace {
        constexpr unsigned slotBase = 0x0FFD0000, slotCount = 128;
        constexpr double timerHz = 62156250.0;
        struct Player {
                std::unique_ptr<Video::MovieDecoder> decoder;
                double position{};
                std::uint64_t started{};
                bool playing{};
                unsigned draws{};
                std::uint64_t lastDisplayed{};
                std::array<unsigned, slotCount> calls{};
                double current(const Interpreter &cpu) const { return position + (playing ? double(cpu.m_scheduler.now() - started) / timerHz : 0); }
        };
        void setState(Interpreter &cpu, unsigned object, unsigned value) { cpu.m_memory.write<unsigned>(object + 0x1C, value); }
        void initView(Interpreter &cpu, unsigned view, unsigned parent, unsigned type)
        {
            for (unsigned off = 0; off < 0xA8; off += 4)
                cpu.m_memory.write<unsigned>(view + off, 0);
            cpu.m_memory.write<unsigned>(view, type);
            cpu.m_memory.write<unsigned>(view + 4, type == 1 ? 5 : 7);
            cpu.m_memory.write<unsigned>(view + 8, 100);
            for (const unsigned matrix: {0xCu, 0x4Cu})
                for (unsigned i = 0; i < 4; ++i)
                    cpu.m_memory.write<unsigned>(view + matrix + i * 20, std::bit_cast<unsigned>(1.f));
            cpu.m_memory.write<unsigned>(view + 0x8C, std::bit_cast<unsigned>(16.f / 9.f));
            cpu.m_memory.write<unsigned>(view + 0x9C, std::bit_cast<unsigned>(1.f));
            cpu.m_memory.write<unsigned>(view + 0xA4, parent);
        }
    } // namespace
    struct State {
            std::unordered_map<unsigned, Player> players;
    };
    namespace {
        State &state(Interpreter &cpu)
        {
            if (!cpu.m_mvPlayer)
                cpu.m_mvPlayer = std::make_shared<State>();
            return *cpu.m_mvPlayer;
        }
        void method(Interpreter &cpu, unsigned slot)
        {
            const unsigned object = cpu.m_gpr[3];
            auto &players = state(cpu).players;
            const auto it = players.find(object);
            cpu.m_gpr[3] = 115; // MVPlayer invalid state / unopened stream
            if (it == players.end())
                return;
            auto &player = it->second;
            const unsigned count = ++player.calls[slot];
            if (std::getenv("WEMU_MVP_TRACE") && count <= 3)
                Utils::Log::error("[MVP] object={:08X} slot={} call={} f1={}", object, slot, count, cpu.m_fpr[1]);
            try {
                switch (slot) {
                    case 3: { // Open(local URI)
                        std::string uri;
                        for (unsigned i = 0; i < 4096; ++i) {
                            const auto *c = cpu.m_memory.hostPtr(cpu.m_gpr[4] + i);
                            if (!c || !*c)
                                break;
                            uri += char(*c);
                        }
                        if (uri.starts_with("file://"))
                            uri.erase(0, 7);
                        const auto path = ResolveFsContentPath(uri);
                        if (path.empty()) {
                            cpu.m_gpr[3] = 65;
                            return;
                        }
                        auto decoder = std::make_unique<Video::MovieDecoder>(path);
                        player.decoder = std::move(decoder);
                        player.position = 0;
                        player.playing = false;
                        player.lastDisplayed = 0;
                        cpu.m_memory.write<unsigned>(object + 0x1A0, unsigned(player.decoder->width() * 1000));
                        cpu.m_memory.write<unsigned>(object + 0x1A4, unsigned(player.decoder->height() * 1000));
                        setState(cpu, object, 0);
                        Utils::Log::error("[MVP] opened {} ({}x{}, {:.3f}s)", uri, player.decoder->width(), player.decoder->height(),
                                          player.decoder->duration());
                        break;
                    }
                    case 5: // Prepare, retaining the current seek position
                    case 9: // Prepare and play
                        if (!player.decoder)
                            return;
                        player.position = player.current(cpu);
                        player.started = cpu.m_scheduler.now();
                        player.playing = slot == 9;
                        setState(cpu, object, slot == 9 ? 3 : 2);
                        break;
                    case 7: // Start/resume playback
                        if (!player.decoder)
                            return;
                        if (!player.playing)
                            player.started = cpu.m_scheduler.now();
                        player.playing = true;
                        setState(cpu, object, 3);
                        break;
                    case 13: // Stop, preserving an open stream for subsequent seeks
                        if (!player.decoder)
                            return;
                        player.position = player.current(cpu);
                        player.playing = false;
                        setState(cpu, object, 2);
                        break;
                    case 15: // Close
                        player.decoder.reset();
                        player.playing = false;
                        setState(cpu, object, unsigned(-2));
                        break;
                    case 17: // Seek(seconds)
                        if (!player.decoder || !std::isfinite(cpu.m_fpr[1]) || cpu.m_fpr[1] < 0) {
                            cpu.m_gpr[3] = 65;
                            return;
                        }
                        player.position = std::min(cpu.m_fpr[1], player.decoder->duration());
                        player.started = cpu.m_scheduler.now();
                        player.decoder->seek(player.position);
                        break;
                    case 19: // Renderer update
                        break;
                    case 21: // Playback position, returned in f1
                        cpu.m_fpr[1] = player.decoder ? std::min(player.current(cpu), player.decoder->duration()) : 0;
                        break;
                    case 23: // Natural size in floating point
                        if (!player.decoder)
                            return;
                        if (cpu.m_memory.hostPtr(cpu.m_gpr[4], 4) && cpu.m_memory.hostPtr(cpu.m_gpr[5], 4)) {
                            cpu.m_memory.write<unsigned>(cpu.m_gpr[4], std::bit_cast<unsigned>(float(player.decoder->width())));
                            cpu.m_memory.write<unsigned>(cpu.m_gpr[5], std::bit_cast<unsigned>(float(player.decoder->height())));
                        }
                        break;
                    case 25: // Last playback error
                        if (!player.decoder)
                            return;
                        break;
                    case 27:
                        break; // Request renderer refresh
                    case 29:
                        cpu.m_fpr[1] = 1;
                        break; // Playback rate
                    case 31:
                        cpu.m_gpr[3] = 0x1BC;
                        return;
                    default:
                        return;
                }
                cpu.m_gpr[3] = 0;
            } catch (const std::exception &error) {
                Utils::Log::error("[MVP] slot {}: {}", slot, error.what());
                player.playing = false;
                setState(cpu, object, unsigned(-2));
                cpu.m_gpr[3] = 65;
            }
        }
    } // namespace
    void create(Interpreter &cpu)
    {
        const auto object = cpu.m_memory.heapAllocate(0x1BC, 8);
        const auto table = cpu.m_memory.heapAllocate(slotCount * 4, 8);
        for (unsigned off = 0; off < 0x1BC; off += 4)
            cpu.m_memory.write<unsigned>(object + off, 0);
        for (unsigned slot = 0; slot < slotCount; ++slot)
            cpu.m_memory.write<unsigned>(table + slot * 4, slot % 2 ? slotBase + slot * 4 : 0);
        cpu.m_memory.write<unsigned>(object + 0x17C, table);
        initView(cpu, object + 0x28, object, 4);
        initView(cpu, object + 0xD0, object, 1);
        state(cpu).players.try_emplace(object);
        cpu.m_gpr[3] = object;
    }
    void destroy(Interpreter &cpu)
    {
        state(cpu).players.erase(cpu.m_gpr[3]);
        cpu.m_gpr[3] = 0;
    }
    void shutdown(Interpreter &cpu)
    {
        cpu.m_mvPlayer.reset();
        cpu.m_gpr[3] = 0;
    }
    void draw(Interpreter &cpu)
    {
        const unsigned view = cpu.m_gpr[3];
        cpu.m_gpr[3] = 0;
        if (!cpu.m_memory.hostPtr(view, 0xA8) || !cpu.m_memory.read<unsigned>(view + 4))
            return;
        const auto object = cpu.m_memory.read<unsigned>(view + 0xA4);
        const auto it = state(cpu).players.find(object);
        if (it == state(cpu).players.end() || !it->second.decoder)
            return;
        auto &player = it->second;
        try {
            const double position = std::min(player.current(cpu), player.decoder->duration());
            const auto frame = player.decoder->frameAt(position);
            if (!frame)
                return;
            if (std::getenv("WEMU_MVP_TRACE") && player.draws++ < 6) {
                std::string matrices;
                for (unsigned i = 0; i < 32; ++i)
                    matrices += std::format(" {}", std::bit_cast<float>(cpu.m_memory.read<unsigned>(view + 0xC + i * 4)));
                Utils::Log::error("[MVP_VIEW] view={:08X} type={} flags={} time={} pixels=({},{},{}) matrices={}", view,
                                  cpu.m_memory.read<unsigned>(view), cpu.m_memory.read<unsigned>(view + 4), frame->timestamp, frame->rgba[0],
                                  frame->rgba[1], frame->rgba[2], matrices);
            }
            if (cpu.m_renderer && player.lastDisplayed != frame->serial) {
                cpu.m_renderer->record_movie_frame();
                player.lastDisplayed = frame->serial;
            }
            Gfx::Gx2Command command{Gfx::Gx2Cmd::DrawImage};
            command.gpr[0] = frame->width;
            command.gpr[1] = frame->height;
            command.payload.resize(32 + frame->rgba.size() / 4);
            for (unsigned i = 0; i < 32; ++i)
                command.payload[i] = cpu.m_memory.read<unsigned>(view + 0xC + i * 4);
            std::memcpy(command.payload.data() + 32, frame->rgba.data(), frame->rgba.size());
            Gfx::gx2Stream().push(command);
            if (player.playing && player.current(cpu) >= player.decoder->duration()) {
                player.position = player.decoder->duration();
                player.playing = false;
                setState(cpu, object, 2);
            }
        } catch (const std::exception &error) {
            Utils::Log::error("[MVP] draw: {}", error.what());
            player.playing = false;
            setState(cpu, object, unsigned(-2));
        }
    }
    void installHooks(Interpreter &cpu)
    {
        for (unsigned slot = 0; slot < slotCount; ++slot)
            cpu.m_hooks[slotBase + slot * 4] = [slot](Interpreter &interpreter) { method(interpreter, slot); };
    }
} // namespace Core::MvPlayer
