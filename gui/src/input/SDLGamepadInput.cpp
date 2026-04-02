#include "SDLGamepadInput.hpp"
#include <QDebug>

SDLGamepadInput::SDLGamepadInput(const int index, QObject *parent)
    : QObject(parent)
{
    m_buttonMap = {
        { "A", SDL_CONTROLLER_BUTTON_A },
        { "B", SDL_CONTROLLER_BUTTON_B },
        { "X", SDL_CONTROLLER_BUTTON_X },
        { "Y", SDL_CONTROLLER_BUTTON_Y },

        { "LB", SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
        { "RB", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },

        { "BACK",  SDL_CONTROLLER_BUTTON_BACK },
        { "START", SDL_CONTROLLER_BUTTON_START },
        { "XBOX_GUIDE", SDL_CONTROLLER_BUTTON_GUIDE},

        { "DPAD_UP",    SDL_CONTROLLER_BUTTON_DPAD_UP },
        { "DPAD_DOWN",  SDL_CONTROLLER_BUTTON_DPAD_DOWN },
        { "DPAD_LEFT",  SDL_CONTROLLER_BUTTON_DPAD_LEFT },
        { "DPAD_RIGHT", SDL_CONTROLLER_BUTTON_DPAD_RIGHT },

        { "LEFTSTICK_BUTTON",  SDL_CONTROLLER_BUTTON_LEFTSTICK },
        { "RIGHTSTICK_BUTTON",  SDL_CONTROLLER_BUTTON_RIGHTSTICK },

    };

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

    for (const QString &name : m_buttonMap.keys()) {
        const auto sdlButton = m_buttonMap.value(name);
        bool now = SDL_GameControllerGetButton(m_controller, sdlButton);

        if (m_lastButtons.value(name) != now)
        {
            m_lastButtons[name] = now;
            qDebug() << name;
            emit buttonStateChanged(name, now);
        }
    }

    auto checkAxis = [&](const QString &name, float &last)
    {
        float v = getAxisValue(name);

        if (qAbs(v - last) > 0.01f) {
            last = v;
            emit axisChanged(name, v);
        }
    };

    checkAxis("LX", m_lastLX);
    checkAxis("LY", m_lastLY);
    checkAxis("RX", m_lastRX);
    checkAxis("RY", m_lastRY);

    auto checkTrigger = [&](const QString &name, SDL_GameControllerAxis axis, bool &last)
    {
        Sint16 value = SDL_GameControllerGetAxis(m_controller, axis);
        bool now = value > 13000;

        if (now != last) {
            last = now;
            qDebug() << name;
            emit buttonStateChanged(name, now);
        }
    };

    checkTrigger("LT", SDL_CONTROLLER_AXIS_TRIGGERLEFT, m_lastLT);
    checkTrigger("RT", SDL_CONTROLLER_AXIS_TRIGGERRIGHT, m_lastRT);
}
bool SDLGamepadInput::isButtonPressed(const QString &buttonName) const
{
    if (!m_controller)
        return false;
    return m_lastButtons.value(buttonName, false);
}

static float normalizeAxis(const Sint16 value)
{
    constexpr float MAX_SINT16 = 32767.0f;
    return qBound(-1.0f, static_cast<float>(value) / MAX_SINT16, 1.0f);
}

float SDLGamepadInput::getAxisValue(const QString &axisName) const
{
    float v = 0.0f;

    if (!m_controller)
        return 0.0f;
    if (axisName == "LX")
        v = normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTX));
    else if (axisName == "LY")
        v = normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTY));
    else if (axisName == "RX")
        v = normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_RIGHTX));
    else if (axisName == "RY")
        v = normalizeAxis(SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_RIGHTY));
    return v;
}
