#pragma once
#include <cstdint>
#include <span>
namespace Core {
    class Interpreter;
}
namespace Core::MvPlayer {
    struct State;
    bool hasPlayingAudio(const Interpreter &cpu);
    void mixAudio(Interpreter &cpu, std::span<float> stereo, std::uint64_t frameTick);
    void create(Interpreter &cpu);
    void destroy(Interpreter &cpu);
    void draw(Interpreter &cpu);
    void shutdown(Interpreter &cpu);
    void installHooks(Interpreter &cpu);
} // namespace Core::MvPlayer
