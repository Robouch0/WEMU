#include "SDLGamepadInput.hpp"
#include <QDebug>

SDLGamepadInput::SDLGamepadInput(int index, QObject *parent)
    : QObject(parent)
{
    m_controller = SDL_GameControllerOpen(index);
    if (!m_controller)
        return;

    SDL_Joystick *joy = SDL_GameControllerGetJoystick(m_controller);
    m_instanceId = SDL_JoystickInstanceID(joy);
}

SDLGamepadInput::~SDLGamepadInput()
{
    if (m_controller)
        SDL_GameControllerClose(m_controller);
}

SDL_JoystickID SDLGamepadInput::instanceId() const
{
    return m_instanceId;
}

QString SDLGamepadInput::name() const
{
    return QString("Gamepad %1").arg(m_instanceId);
}

void SDLGamepadInput::update()
{
    SDL_GameControllerUpdate();
}

bool SDLGamepadInput::isButtonPressed(const QString &buttonName) const
{
    if (!m_controller)
        return false;

    if (buttonName == "A")
        return SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_A);
    if (buttonName == "B")
        return SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_B);
    if (buttonName == "X")
        return SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_X);
    if (buttonName == "Y")
        return SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_Y);
    if (buttonName == "Start")
        return SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_START);
    if (buttonName == "Back")
        return SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_BACK);

    return false;
}

static float normalizeAxis(Sint16 value)
{
    constexpr float MAX_SINT16 = 32767.0f;
    return qBound(-1.0f, static_cast<float>(value) / MAX_SINT16, 1.0f);
}

float SDLGamepadInput::getAxisValue(const QString &axisName) const
{
    if (!m_controller)
        return 0.0f;

    if (axisName == "LX")
        return normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTX));
    if (axisName == "LY")
        return normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTY));
    if (axisName == "RX")
        return normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_RIGHTX));
    if (axisName == "RY")
        return normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_RIGHTY));

    return 0.0f;
}
