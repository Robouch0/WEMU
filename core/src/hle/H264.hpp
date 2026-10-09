#pragma once

#include <cstdint>

namespace Core { class Interpreter; }
namespace Core::H264 {
    struct State;
    inline constexpr std::uint32_t callbackSentinel = 0x0FFFFFE8;
    void onCallbackReturn(Interpreter &cpu);
}

void RegisterH264Functions();
