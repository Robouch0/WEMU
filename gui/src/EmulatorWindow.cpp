#include "EmulatorWindow.hpp"
#include <QDebug>

EmulatorWindow::EmulatorWindow(QWindow* parent)
    : QQuickWindow(parent)
{
    m_manager = new InputManager(this);
    m_keyboard = new KeyboardInputDevice(this);

    connect(m_keyboard, &KeyboardInputDevice::keyPressed,
            m_manager, &InputManager::onKeyPressed);
    connect(m_keyboard, &KeyboardInputDevice::keyReleased,
            m_manager, &InputManager::onKeyReleased);
}

void EmulatorWindow::keyPressEvent(QKeyEvent* event) {
    if (m_keyboard) m_keyboard->handleKeyPress(event);
}

void EmulatorWindow::keyReleaseEvent(QKeyEvent* event) {
    if (m_keyboard) m_keyboard->handleKeyRelease(event);
}
