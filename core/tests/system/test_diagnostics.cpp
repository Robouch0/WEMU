#include <cstdlib>
#include <gtest/gtest.h>
#include <unistd.h>

#include "cpu/interpreter/Interpreter.hpp"
#include "utils/Diagnostics.hpp"

TEST(DiagnosticsDeathTest, DisabledHeartbeatDoesNotFault)
{
    // Run each setting in a child so the cached interval and environment stay isolated.
    for (const char *setting: {"0", static_cast<const char *>(nullptr)}) {
        EXPECT_EXIT(
                {
                    const auto result = setting ? setenv("WEMU_HEARTBEAT", setting, 1) : unsetenv("WEMU_HEARTBEAT");
                    if (result != 0)
                        _exit(1);
                    Core::Interpreter cpu(Core::Binary{});
                    Core::Diag::pollHeartbeat(cpu, 0);
                    Core::Diag::pollHeartbeat(cpu, 0);
                    _exit(0);
                },
                ::testing::ExitedWithCode(0), "");
    }
}
