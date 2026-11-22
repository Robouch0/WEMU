#pragma once
#include <QMap>
#include <QString>
#include <Qt>
#include <array>
#include <cmath>
#include <utility>

// Host controllers (SDL names, see SDLGamepadInput) to Wii U GamePad (VPAD) input for a game session.
namespace ControllerMapping {
    struct State {
            quint32 buttons{}; // VPAD bits
            std::array<float, 2> leftStick{}, rightStick{}; // -1..1, +y is up
            bool operator==(const State &) const = default;
    };

    template <typename Pressed> State keyboard(Pressed &&pressed)
    {
        State state;
        constexpr std::pair<int, quint32> keys[] = {
            {Qt::Key_Return, 0x8000}, {Qt::Key_Enter, 0x8000}, {Qt::Key_J, 0x8000},
            {Qt::Key_Backspace, 0x4000}, {Qt::Key_K, 0x4000}, {Qt::Key_Space, 0x2000}, {Qt::Key_I, 0x1000},
            {Qt::Key_Left, 0x0800}, {Qt::Key_Right, 0x0400}, {Qt::Key_Up, 0x0200}, {Qt::Key_Down, 0x0100},
            {Qt::Key_Shift, 0x0080}, {Qt::Key_Control, 0x0040}, {Qt::Key_Q, 0x0020}, {Qt::Key_E, 0x0010},
            {Qt::Key_P, 0x0008}, {Qt::Key_M, 0x0004},
        };
        for (const auto &[key, bit]: keys)
            if (pressed(key))
                state.buttons |= bit;
        state.leftStick = {float(pressed(Qt::Key_D)) - float(pressed(Qt::Key_A)),
                           float(pressed(Qt::Key_W)) - float(pressed(Qt::Key_S))};
        const auto length = std::hypot(state.leftStick[0], state.leftStick[1]);
        if (length > 1)
            for (auto &axis: state.leftStick)
                axis /= length;
        return state;
    }

    // Wii U buttons an InputProfile binds (InputProfileManager's keys) and their VPAD bits.
    inline constexpr std::pair<const char *, quint32> boundButtons[] = {
        {"A", 0x8000}, {"B", 0x4000}, {"X", 0x2000}, {"Y", 0x1000},
        {"DPAD_LEFT", 0x0800}, {"DPAD_RIGHT", 0x0400}, {"DPAD_UP", 0x0200}, {"DPAD_DOWN", 0x0100},
        {"ZL", 0x0080}, {"ZR", 0x0040}, {"L", 0x0020}, {"R", 0x0010},
    };
    // Controller buttons the Settings page does not rebind.
    inline constexpr std::pair<const char *, quint32> fixedButtons[] = {
        {"START", 0x0008},
        {"BACK", 0x0004},
        {"RIGHTSTICK_BUTTON", 0x20000},
        {"LEFTSTICK_BUTTON", 0x40000},
    };

    // pressed(controllerButton) -> bool.
    template <typename Pressed> quint32 vpadButtons(const QMap<QString, QString> &bindings, Pressed &&pressed)
    {
        quint32 buttons{};
        for (const auto &[wiiu, bit]: boundButtons) {
            const auto bound = bindings.value(QString::fromLatin1(wiiu));
            if (!bound.isEmpty() && pressed(bound))
                buttons |= bit;
        }
        for (const auto &[button, bit]: fixedButtons)
            if (pressed(QString::fromLatin1(button)))
                buttons |= bit;
        return buttons;
    }

    // SDL axes (-1..1, +y down) to a VPAD stick: radial dead zone, rescaled so a full tilt still reads 1.
    inline std::array<float, 2> stick(float x, float y, float deadZone = 0.15f)
    {
        const float length = std::hypot(x, y);
        if (length <= deadZone)
            return {};
        const float scale = std::fmin(1.0f, (length - deadZone) / (1.0f - deadZone)) / length;
        return {x * scale, -y * scale};
    }

    inline float magnitude(const std::array<float, 2> &stick) { return stick[0] * stick[0] + stick[1] * stick[1]; }

    // Several controllers (or a controller and the phone): the stick pushed furthest wins.
    inline const std::array<float, 2> &stronger(const std::array<float, 2> &a, const std::array<float, 2> &b)
    {
        return magnitude(b) > magnitude(a) ? b : a;
    }
} // namespace ControllerMapping
