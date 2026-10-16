#pragma once
namespace Core {
    class Interpreter;
}
namespace Core::MvPlayer {
    struct State;
    void create(Interpreter &cpu);
    void destroy(Interpreter &cpu);
    void draw(Interpreter &cpu);
    void shutdown(Interpreter &cpu);
    void installHooks(Interpreter &cpu);
} // namespace Core::MvPlayer
