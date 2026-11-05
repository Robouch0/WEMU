#include "MenuKeyBridge.hpp"
#include <QCoreApplication>
#include <QKeyEvent>
#include <QtMath>
#include <utility>

namespace {
constexpr int firstRepeatMs = 350;
constexpr int nextRepeatMs = 140;
constexpr float stickPress = 0.5f;
constexpr float stickRelease = 0.25f;

int keyForButton(const QString &button) {
  if (button == "Up")
    return Qt::Key_Up;
  if (button == "Down")
    return Qt::Key_Down;
  if (button == "Left")
    return Qt::Key_Left;
  if (button == "Right")
    return Qt::Key_Right;
  if (button == "A")
    return Qt::Key_Space;
  if (button == "B")
    return Qt::Key_Escape;
  if (button == "LB")
    return Qt::Key_PageUp;
  if (button == "RB")
    return Qt::Key_PageDown;
  return 0;
}

bool isDirection(int key) {
  return key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
         key == Qt::Key_Right;
}
} // namespace

MenuKeyBridge::MenuKeyBridge(std::function<bool()> menusActive, QObject *parent)
    : QObject(parent), m_menusActive(std::move(menusActive)) {
  connect(&m_repeat, &QTimer::timeout, this, [this] {
    m_repeat.setInterval(nextRepeatMs);
    send(m_repeatKey, true, true);
  });
}

void MenuKeyBridge::setSuspended(bool suspended) {
  if (m_suspended == suspended)
    return;
  m_suspended = suspended;
  if (suspended)
    m_repeat.stop();
  emit suspendedChanged();
}

void MenuKeyBridge::onButtonChanged(const QString &button, bool pressed) {
  const int key = keyForButton(button);
  if (!key)
    return;
  pressed ? press(key) : release(key);
}

void MenuKeyBridge::onAxisChanged(const QString &axis, float value) {
  if (axis == "LX")
    stick(m_stickX, value, Qt::Key_Left, Qt::Key_Right);
  else if (axis == "LY")
    stick(m_stickY, value, Qt::Key_Up, Qt::Key_Down);
}

void MenuKeyBridge::stick(int &current, float value, int negativeKey,
                          int positiveKey) {
  if (current && qAbs(value) < stickRelease) {
    release(current);
    current = 0;
  } else if (!current && qAbs(value) >= stickPress) {
    current = value < 0 ? negativeKey : positiveKey;
    press(current);
  }
}

void MenuKeyBridge::press(int key) {
  if (m_suspended || !m_menusActive())
    return;
  // The D-pad and the stick can hold the same direction; the key is down until
  // both let go.
  if (m_held[key]++ > 0)
    return;
  send(key, true);
  if (isDirection(key)) {
    m_repeatKey = key;
    m_repeat.start(firstRepeatMs);
  }
}

void MenuKeyBridge::release(int key) {
  const auto held = m_held.find(key);
  // Presses swallowed while suspended or in a game have no matching release to
  // send.
  if (held == m_held.end())
    return;
  if (--held.value() > 0)
    return;
  m_held.erase(held);
  if (m_repeatKey == key) {
    m_repeat.stop();
    m_repeatKey = 0;
  }
  if (m_menusActive())
    send(key, false);
}

void MenuKeyBridge::send(int key, bool pressed, bool autoRepeat) {
  if (!m_window || !m_window->isVisible())
    return;
  if (autoRepeat && (m_suspended || !m_menusActive())) {
    m_repeat.stop();
    return;
  }
  QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, key,
                  Qt::NoModifier, QString(), autoRepeat);
  QCoreApplication::sendEvent(m_window, &event);
}
