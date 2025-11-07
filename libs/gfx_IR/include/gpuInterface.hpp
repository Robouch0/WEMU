#pragma once
#include <cstdint>
#include <vector>

namespace graphics_frontend {

    struct GraphicsIR {
        uint32_t commandId;   // e.g. DrawCall, SetTexture, etc.
        std::vector<uint64_t> params;
    };

    class GfxTranslator {
        public:
            static GraphicsIR translateCommand(uint32_t rawCommand);
    };

}
