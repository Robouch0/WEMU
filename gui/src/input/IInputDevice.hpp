#pragma once
#include <QObject>
#include <QString>

struct ButtonState {
    bool pressed = false;
};

struct AxisState {
    double value = 0.0;
};

class IInputDevice : public QObject {
    Q_OBJECT
public:
        explicit IInputDevice(QObject* parent = nullptr) : QObject(parent) {}
        virtual ~IInputDevice() = default;

        virtual QString deviceName() const = 0;
        virtual void start() = 0;
        virtual void stop() = 0;

        signals:
            void buttonChanged(int buttonId, ButtonState state);
            void axisChanged(int axisId, AxisState state);
            void deviceDisconnected();
};
