#pragma once
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

    private:
        SDL_GameController *m_controller = nullptr;
        SDL_JoystickID m_instanceId = -1;
};
