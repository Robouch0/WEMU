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
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            const SDL_JoystickID id = e.cdevice.which;

            for (auto it = m_devices.begin(); it != m_devices.end(); ) {
                auto* pad = dynamic_cast<SDLGamepadInput*>(*it);

                if (pad && pad->instanceId() == id) {
                    it = m_devices.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

void InputManager::addDevice(IInputDevice *device)
{
    if (!device) return;

    m_devices.append(device);
    qDebug() << "Added device:" << device->name();

    if (auto *pad = dynamic_cast<SDLGamepadInput *>(device)) {
        connect(pad, &SDLGamepadInput::buttonStateChanged,this,
            [this, pad](const QString &btn, const bool pressed) {
                    emit buttonChanged(btn, pressed, pad->name());
                });

        connect(pad, &SDLGamepadInput::axisChanged,this,
            [this, pad](const QString &axis, const float value) {
                    emit axisChanged(axis, value, pad->name());
                });
    }
}

void InputManager::removeDevice(const QString &deviceName)
{
    for (auto it = m_devices.begin(); it != m_devices.end(); ) {
        if ((*it)->name() == deviceName) {
            it = m_devices.erase(it);
        } else {
            ++it;
        }
    }
}

IInputDevice *InputManager::getDevice(const QString &deviceName) const
{
    for (auto *d: m_devices)
        if (d->name() == deviceName)
            return d;
    return nullptr;
}

void InputManager::updateAll()
{
    for (auto *device: m_devices) {
        device->update();
    }

    emit inputUpdated();
}

bool InputManager::isButtonPressed(const QString &buttonName) const
{
    for (const auto *device: m_devices) {
        if (device->isButtonPressed(buttonName)) {
            return true;
        }
    }
    return false;
}

float InputManager::getAxis(const QString &axisName) const
{
    for (const auto *device: m_devices) {
        float v = device->getAxisValue(axisName);
        if (qAbs(v) > 0.01f)
            return v;
    }

    return 0.0f;
}

bool InputManager::isButtonReleased(const QString &buttonName) const
{
    for (const auto *device: m_devices) {
        if (device->isButtonReleased(buttonName))
            return true;
    }
    return false;
}

QStringList InputManager::connectedDevices() const
{
    QStringList names;

    for (const auto *device: m_devices)
        names.append(device->name());
    return names;
}
