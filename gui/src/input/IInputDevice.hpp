#pragma once
#include <QString>

class IInputDevice {
    public:
        virtual ~IInputDevice() = default;

        virtual void update() = 0;

        [[nodiscard]] virtual bool isButtonPressed(const QString &buttonName) const = 0;
        [[nodiscard]] virtual bool isButtonReleased(const QString &buttonName) const = 0;
        [[nodiscard]] virtual float getAxisValue(const QString &axisName) const = 0;

        [[nodiscard]] virtual QString name() const = 0;
};
