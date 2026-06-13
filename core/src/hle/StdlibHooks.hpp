#pragma once

namespace Core {
    class Interpreter;
    struct Binary;

    void installStdlibHooks(Interpreter &interp, const Binary &binary);
} // namespace Core
