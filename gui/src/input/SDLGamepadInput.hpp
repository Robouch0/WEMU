#pragma once
#include <QMap>
#include <QObject>
#include <SDL2/SDL.h>
#include "IInputDevice.hpp"


class SDLGamepadInput : public QObject, public IInputDevice {
    Q_OBJECT

public:
        explicit SDLGamepadInput(int index, QObject *parent = nullptr);
        ~SDLGamepadInput();

        void update() override;
        bool isButtonPressed(const QString &buttonName) const override;
        bool isButtonReleased(const QString &) const override { return false; }
        float getAxisValue(const QString &axisName) const override;
        QString name() const override;

        SDL_JoystickID instanceId() const;

        signals:
            void buttonStateChanged(QString button, bool pressed);
            void axisChanged(QString axis, float value);

    private:
        SDL_GameController *m_controller = nullptr;
        QMap<QString, SDL_GameControllerButton> m_buttonMap;
        QMap<QString, bool> m_lastButtons;
        float m_lastLX = 0.0f;
        float m_lastLY = 0.0f;
        float m_lastRX = 0.0f;
        float m_lastRY = 0.0f;

        SDL_JoystickID m_instanceId = -1;
};
