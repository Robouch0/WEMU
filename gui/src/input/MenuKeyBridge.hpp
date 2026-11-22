#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWindow>
#include <functional>

// Turns controller input into key events for the menus, so controls behave the
// same from a controller and a keyboard: arrows move, A is Space, B is Escape,
// LB and RB are Page Up and Down.
class MenuKeyBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      bool suspended READ suspended WRITE setSuspended NOTIFY suspendedChanged)

public:
  explicit MenuKeyBridge(std::function<bool()> menusActive,
                         QObject *parent = nullptr);

  void setWindow(QWindow *window) { m_window = window; }
  [[nodiscard]] bool suspended() const { return m_suspended; }
  // Lets a page read raw buttons, such as while it captures a binding.
  void setSuspended(bool suspended);

public slots:
  void onButtonChanged(const QString &button, bool pressed);
  void onAxisChanged(const QString &axis, float value);

signals:
  void suspendedChanged();

private:
  void press(int key);
  void release(int key);
  void send(int key, bool pressed, bool autoRepeat = false);
  void stick(int &current, float value, int negativeKey, int positiveKey);

  std::function<bool()> m_menusActive;
  QPointer<QWindow> m_window;
  QTimer m_repeat;
  QHash<int, int> m_held;
  int m_repeatKey = 0;
  int m_stickX = 0;
  int m_stickY = 0;
  bool m_suspended = false;
};
