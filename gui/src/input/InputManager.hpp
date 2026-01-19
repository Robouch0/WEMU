// InputManager.hpp
#pragma once
#include <QVector>
#include <QMap>
#include <QTimer>
#include "IInputDevice.hpp"

class InputManager final : public QObject {
    Q_OBJECT

public:
        explicit InputManager(QObject *parent = nullptr);

        void addDevice(IInputDevice *device);
        void removeDevice(const QString &deviceName);
        IInputDevice* getDevice(const QString &deviceName) const;

        Q_INVOKABLE bool isButtonPressed(const QString &buttonName) const;
        Q_INVOKABLE bool isButtonReleased(const QString &buttonName) const;
        Q_INVOKABLE QStringList connectedDevices() const;

        signals:
            void inputUpdated();
        void buttonChanged(QString buttonName, bool pressed, QString deviceName);

    private slots:
        void updateAll();
        void pollSDLEvents();

    private:
        QVector<IInputDevice*> m_devices;
        QTimer m_updateTimer;
};
