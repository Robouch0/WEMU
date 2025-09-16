#include "gpuInterface.hpp"

namespace graphics_frontend {

    GraphicsIR GfxTranslator::translateCommand(uint32_t rawCommand) {
        GraphicsIR ir;
        ir.commandId = rawCommand;
        ir.params = {}; // dummy
        return ir;
    }

}
