#include "InputProfileManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

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

    load();
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
    save();
    emit bindingsChanged();
}

QString InputProfileManager::getBinding(const QString &wiiuButton) const
{
    return m_profile.bindings.value(wiiuButton, "");
}

QString InputProfileManager::configPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/bindings.json";
}

void InputProfileManager::save() const
{
    QJsonObject bindings;
    for (auto it = m_profile.bindings.constBegin(); it != m_profile.bindings.constEnd(); ++it)
        bindings[it.key()] = it.value();

    QJsonObject root;
    root["name"]     = m_profile.name;
    root["bindings"] = bindings;

    QFile file(configPath());
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(root).toJson());
}

void InputProfileManager::load()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject bindings = root["bindings"].toObject();

    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        if (m_profile.bindings.contains(it.key()))
            m_profile.bindings[it.key()] = it.value().toString();
    }
}
