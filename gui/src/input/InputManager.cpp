#include "InputManager.hpp"

InputManager::InputManager(QObject* parent)
    : QObject(parent)
{
    m_keyboard = new KeyboardInputDevice(this);

    connect(m_keyboard, &KeyboardInputDevice::keyPressed, this, &InputManager::onKeyPressed);
    connect(m_keyboard, &KeyboardInputDevice::keyReleased, this, &InputManager::onKeyReleased);
}
