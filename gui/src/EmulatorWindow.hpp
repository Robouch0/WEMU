#pragma once
#include <QQuickWindow>
#include "input/InputManager.hpp"
#include "input/KeyboardInputDevice.hpp"

class EmulatorWindow : public QQuickWindow {
    Q_OBJECT
public:
        explicit EmulatorWindow(QWindow* parent = nullptr);

    protected:
        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;

    private:
        InputManager* m_manager;
        KeyboardInputDevice* m_keyboard;
};
