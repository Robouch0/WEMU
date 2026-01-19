#include "InputManager.hpp"

#include <iostream>
#include <QDebug>
#include <SDL2/SDL.h>
#include "SDLGamepadInput.hpp"

#include "KeyboardInput.hpp"

InputManager::InputManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_updateTimer, &QTimer::timeout, this, [this]() {
       pollSDLEvents();
       updateAll();
   });
    m_updateTimer.start(16);
}


void InputManager::pollSDLEvents()
{
    SDL_Event e;

    SDL_PumpEvents();

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            addDevice(new SDLGamepadInput(e.cdevice.which, this));
        }
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            const SDL_JoystickID id = e.cdevice.which;

            m_devices.erase(
                std::remove_if(m_devices.begin(), m_devices.end(),
                    [&](IInputDevice *d) {
                        auto *pad = dynamic_cast<SDLGamepadInput *>(d);
                        return pad && pad->instanceId() == id;
                    }),
                m_devices.end());
        }
    }
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
