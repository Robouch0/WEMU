#include "InputManager.hpp"

#include <iostream>
#include <QDebug>

#include "KeyboardInput.hpp"

InputManager::InputManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_updateTimer, &QTimer::timeout, this, &InputManager::updateAll);
    m_updateTimer.start(16);
}

void InputManager::addDevice(IInputDevice *device) {
    if (!device) return;
    m_devices.append(device);
    qDebug() << "Added device:" << device->name();
}

void InputManager::removeDevice(const QString &deviceName) {
    m_devices.erase(std::ranges::remove_if(m_devices,
                                           [&](IInputDevice* d){ return d->name() == deviceName; }).begin(),
                    m_devices.end());
}

IInputDevice* InputManager::getDevice(const QString &deviceName) const {
    for (auto *d : m_devices)
        if (d->name() == deviceName)
            return d;
    return nullptr;
}

void InputManager::updateAll() {
    for (auto *device : m_devices) {
        device->update();
    }

    emit inputUpdated();
}

bool InputManager::isButtonPressed(const QString &buttonName) const {
    for (const auto *device : m_devices) {
        if (device->isButtonPressed(buttonName)) {
            return true;
        }
    }
    return false;
}

bool InputManager::isButtonReleased(const QString &buttonName) const {
    for (const auto *device : m_devices) {
        if (device->isButtonReleased(buttonName))
            return true;
    }
    return false;
}

QStringList InputManager::connectedDevices() const {
    QStringList names;
    for (const auto *device : m_devices)
        names.append(device->name());
    return names;
}
