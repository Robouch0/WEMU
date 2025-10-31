#pragma once
#include <QObject>
#include <QGuiApplication>
#include "KeyboardInputDevice.hpp"

class InputManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString keyboardLayout READ keyboardLayout CONSTANT)

public:
    explicit InputManager(QObject* parent = nullptr);

    QString keyboardLayout() const {
        return QGuiApplication::inputMethod()->locale().name(); // why its always returning US en here ??
    }

public slots:
    void onKeyPressed(Qt::Key, const QString &text) { emit keyChanged(text, true); }
    void onKeyReleased(Qt::Key, const QString &text) { emit keyChanged(text, false); }

signals:
    void keyChanged(QString keyText, bool pressed);

private:
    KeyboardInputDevice* m_keyboard;
};
