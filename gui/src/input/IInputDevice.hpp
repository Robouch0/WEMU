#pragma once
#include <QString>

class IInputDevice {
    public:
        virtual ~IInputDevice() = default;

        virtual void update() = 0;

        virtual bool isButtonPressed(const QString &buttonName) const = 0;
        virtual bool isButtonReleased(const QString &buttonName) const = 0;
        virtual float getAxisValue(const QString &axisName) const = 0;

        virtual QString name() const = 0;
};
