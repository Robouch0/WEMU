#include "KeyboardInputDevice.hpp"
#include <QDebug>

KeyboardInputDevice::KeyboardInputDevice(QObject* parent)
    : IInputDevice(parent)
{}

void KeyboardInputDevice::handleKeyPress(QKeyEvent* event) {
    if (event->isAutoRepeat()) return;

    int key = event->key();
    QString text = event->text();

    if (!m_pressedKeys.contains(key)) {
        m_pressedKeys.insert(key);
        emit keyPressed(static_cast<Qt::Key>(key), text);
        qDebug() << "Key pressed:" << key << "(" << text << ")";
    }
}

void KeyboardInputDevice::handleKeyRelease(QKeyEvent* event) {
    if (event->isAutoRepeat()) return;

    int key = event->key();
    QString text = event->text();

    if (m_pressedKeys.remove(key)) {
        emit keyReleased(static_cast<Qt::Key>(key), text);
        qDebug() << "Key released:" << key << "(" << text << ")";
    }
}
