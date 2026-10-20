#include <QSet>
#include <gtest/gtest.h>

#include "ControllerMapping.hpp"

namespace {
    QMap<QString, QString> defaultBindings()
    {
        return {
            {"A", "A"}, {"B", "B"}, {"X", "X"}, {"Y", "Y"},
            {"DPAD_UP", "Up"}, {"DPAD_DOWN", "Down"}, {"DPAD_LEFT", "Left"}, {"DPAD_RIGHT", "Right"},
            {"L", "LB"}, {"R", "RB"}, {"ZL", "LT"}, {"ZR", "RT"},
        };
    }

    quint32 buttonsFor(const QMap<QString, QString> &bindings, const QSet<QString> &held)
    {
        return ControllerMapping::vpadButtons(bindings, [&](const QString &button) { return held.contains(button); });
    }
} // namespace

TEST(ControllerMappingTest, DefaultProfileMapsEveryControllerButtonToItsVpadBit)
{
    const auto bindings = defaultBindings();
    EXPECT_EQ(buttonsFor(bindings, {}), 0u);
    EXPECT_EQ(buttonsFor(bindings, {"A"}), 0x8000u);
    EXPECT_EQ(buttonsFor(bindings, {"B", "X", "Y"}), 0x4000u | 0x2000u | 0x1000u);
    EXPECT_EQ(buttonsFor(bindings, {"Left", "Right", "Up", "Down"}), 0x0800u | 0x0400u | 0x0200u | 0x0100u);
    EXPECT_EQ(buttonsFor(bindings, {"LT", "RT", "LB", "RB"}), 0x0080u | 0x0040u | 0x0020u | 0x0010u);
    EXPECT_EQ(buttonsFor(bindings, {"START", "BACK"}), 0x0008u | 0x0004u);
    EXPECT_EQ(buttonsFor(bindings, {"RIGHTSTICK_BUTTON", "LEFTSTICK_BUTTON"}), 0x20000u | 0x40000u);
    // The Guide button opens the session menu, it is no VPAD button.
    EXPECT_EQ(buttonsFor(bindings, {"XBOX_GUIDE"}), 0u);
}

TEST(ControllerMappingTest, RebindingFollowsTheProfile)
{
    auto bindings = defaultBindings();
    bindings["A"] = "B";
    bindings["B"] = "A";
    bindings["ZR"] = "RB";
    bindings["R"] = "RT";
    EXPECT_EQ(buttonsFor(bindings, {"A"}), 0x4000u);
    EXPECT_EQ(buttonsFor(bindings, {"B"}), 0x8000u);
    EXPECT_EQ(buttonsFor(bindings, {"RB"}), 0x0040u);
    EXPECT_EQ(buttonsFor(bindings, {"RT"}), 0x0010u);
    bindings["X"] = "";
    EXPECT_EQ(buttonsFor(bindings, {"X", ""}), 0u);
}

TEST(ControllerMappingTest, SticksHaveADeadZoneAndPointUp)
{
    using ControllerMapping::stick;
    EXPECT_EQ(stick(0.1f, -0.1f), (std::array<float, 2>{}));
    const auto up = stick(0.0f, -1.0f);
    EXPECT_FLOAT_EQ(up[0], 0.0f);
    EXPECT_FLOAT_EQ(up[1], 1.0f);
    const auto right = stick(1.0f, 0.0f);
    EXPECT_FLOAT_EQ(right[0], 1.0f);
    EXPECT_FLOAT_EQ(right[1], 0.0f);
    // Rescaled from the dead zone edge, and never beyond the unit circle.
    EXPECT_NEAR(stick(0.575f, 0.0f)[0], 0.5f, 1e-5f);
    const auto corner = stick(1.0f, 1.0f);
    EXPECT_NEAR(ControllerMapping::magnitude(corner), 1.0f, 1e-5f);
    EXPECT_LT(corner[1], 0.0f);
}

TEST(ControllerMappingTest, TheStickPushedFurthestWins)
{
    const std::array<float, 2> phone{0.2f, 0.0f}, pad{0.0f, -0.6f}, idle{};
    EXPECT_EQ(ControllerMapping::stronger(phone, pad), pad);
    EXPECT_EQ(ControllerMapping::stronger(pad, phone), pad);
    EXPECT_EQ(ControllerMapping::stronger(phone, idle), phone);
}
