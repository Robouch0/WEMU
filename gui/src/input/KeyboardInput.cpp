#include "KeyboardInput.hpp"
#include <qguiapplication.h>
#include <QKeyEvent>

KeyboardInput::KeyboardInput(QObject *parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
}

bool KeyboardInput::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = dynamic_cast<QKeyEvent*>(event);
        onKeyPressed(keyEvent->key());
    } else if (event->type() == QEvent::KeyRelease) {
        const auto *keyEvent = dynamic_cast<QKeyEvent*>(event);
        onKeyReleased(keyEvent->key());
    }
    return QObject::eventFilter(obj, event);
}

void KeyboardInput::onKeyPressed(const int key) {
    auto it = m_keyStates.find(key);
    if (it == m_keyStates.end() || it.value() != KeyState::Pressed) {
        m_keyStates[key] = KeyState::Pressed;
        emit keyStateChanged(QKeySequence(key).toString(), true);
    }
}

void KeyboardInput::onKeyReleased(const int key) {
    m_keyStates[key] = KeyState::Released;
    emit keyStateChanged(QKeySequence(key).toString(), false);
}

void KeyboardInput::update()
{
}

bool KeyboardInput::isButtonPressed(const QString &buttonName) const {
    for (auto it = m_keyStates.begin(); it != m_keyStates.end(); ++it) {
        QString keyName = QKeySequence(it.key()).toString();
        if (keyName.compare(buttonName, Qt::CaseInsensitive) == 0) {
            return it.value() == KeyState::Pressed || it.value() == KeyState::Held;
        }
    }
    return false;
}

bool KeyboardInput::isButtonReleased(const QString &buttonName) const
{
    int key = Qt::Key_A;
    const auto it = m_keyStates.find(key);
    return it != m_keyStates.end() && it.value() == KeyState::Released;
}

QStringList KeyboardInput::allKeys()
{
    QStringList keys;
    for (int k = Qt::Key_Space; k <= Qt::Key_Z; ++k)
        keys << QKeySequence(k).toString();
    return keys;
}


float KeyboardInput::getAxisValue(const QString &) const
{
    return 0.0f;
}
