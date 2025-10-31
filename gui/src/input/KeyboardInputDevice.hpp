#pragma once
#include "IInputDevice.hpp"
#include <QSet>
#include <QKeyEvent>

class KeyboardInputDevice : public IInputDevice {
    Q_OBJECT
public:
    explicit KeyboardInputDevice(QObject* parent = nullptr);

    QString deviceName() const override { return "Keyboard"; }
    void start() override {}
    void stop() override {}

    void handleKeyPress(QKeyEvent* event);
    void handleKeyRelease(QKeyEvent* event);

signals:
    void keyPressed(Qt::Key key, QString text);
    void keyReleased(Qt::Key key, QString text);

private:
    QSet<int> m_pressedKeys;
};
