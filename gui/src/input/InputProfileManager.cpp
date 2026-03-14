#include "InputProfileManager.hpp"

const QStringList InputProfileManager::s_displayOrder = {
    "A", "B", "X", "Y",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "L", "R", "ZL", "ZR",
};

InputProfileManager::InputProfileManager(QObject *parent)
    : QObject(parent)
{
    m_profile.name = "Default";
    m_profile.bindings = {
        { "A",          "A"         },
        { "B",          "B"         },
        { "X",          "X"         },
        { "Y",          "Y"         },
        { "DPAD_UP",    "DPAD_UP"   },
        { "DPAD_DOWN",  "DPAD_DOWN" },
        { "DPAD_LEFT",  "DPAD_LEFT" },
        { "DPAD_RIGHT", "DPAD_RIGHT"},
        { "L",          "LB"        },
        { "R",          "RB"        },
        { "ZL",         "LT"        },
        { "ZR",         "RT"        },
    };
}

QVariantList InputProfileManager::bindingModel() const
{
    QVariantList result;

    for (const auto &wiiu : s_displayOrder) {
        QVariantMap entry;
        entry["wiiu"]       = wiiu;
        entry["wiiuLabel"]  = wiiu;
        entry["xboxButton"] = m_profile.bindings.value(wiiu, "");
        result.append(entry);
    }

    return result;
}

void InputProfileManager::setBinding(const QString &wiiuButton, const QString &xboxButton)
{
    const QString oldXbox = m_profile.bindings.value(wiiuButton);

    for (auto it = m_profile.bindings.begin(); it != m_profile.bindings.end(); ++it) {
        if (it.value() == xboxButton && it.key() != wiiuButton) {
            it.value() = oldXbox;
            break;
        }
    }

    m_profile.bindings[wiiuButton] = xboxButton;
    emit bindingsChanged();
}

QString InputProfileManager::getBinding(const QString &wiiuButton) const
{
    return m_profile.bindings.value(wiiuButton, "");
}
