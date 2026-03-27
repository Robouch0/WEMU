#include "InputProfileManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFileInfo>

const QStringList InputProfileManager::s_displayOrder = {
    "A", "B", "X", "Y",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "L", "R", "ZL", "ZR",
};

InputProfileManager::InputProfileManager(QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < MAX_PROFILES; ++i)
        m_profiles.append(createDefaultProfile("Profile " + QString::number(i + 1)));

    load();
}

InputProfile InputProfileManager::createDefaultProfile(const QString &name)
{
    InputProfile profile;
    profile.name = name;
    profile.bindings = {
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
    return profile;
}

QVariantList InputProfileManager::bindingModel() const
{
    QVariantList result;
    const auto &profile = m_profiles[m_currentIndex];

    for (const auto &wiiu : s_displayOrder) {
        QVariantMap entry;
        entry["wiiu"]       = wiiu;
        entry["wiiuLabel"]  = wiiu;
        entry["xboxButton"] = profile.bindings.value(wiiu, "");
        result.append(entry);
    }

    return result;
}

int InputProfileManager::currentProfileIndex() const
{
    return m_currentIndex;
}

void InputProfileManager::selectProfile(int index)
{
    if (index < 0 || index >= MAX_PROFILES || index == m_currentIndex)
        return;

    m_currentIndex = index;
    save();
    emit bindingsChanged();
}

void InputProfileManager::setBinding(const QString &wiiuButton, const QString &xboxButton)
{
    auto &bindings = m_profiles[m_currentIndex].bindings;
    const QString oldXbox = bindings.value(wiiuButton);

    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        if (it.value() == xboxButton && it.key() != wiiuButton) {
            it.value() = oldXbox;
            break;
        }
    }

    bindings[wiiuButton] = xboxButton;
    save();
    emit bindingsChanged();
}

QString InputProfileManager::getBinding(const QString &wiiuButton) const
{
    return m_profiles[m_currentIndex].bindings.value(wiiuButton, "");
}

QString InputProfileManager::configPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + "/bindings.json";
}

void InputProfileManager::save() const
{
    QJsonArray profilesArray;

    for (const auto &profile : m_profiles) {
        QJsonObject bindings;
        for (auto it = profile.bindings.constBegin(); it != profile.bindings.constEnd(); ++it)
            bindings[it.key()] = it.value();

        QJsonObject obj;
        obj["name"]     = profile.name;
        obj["bindings"] = bindings;
        profilesArray.append(obj);
    }

    QJsonObject root;
    root["currentIndex"] = m_currentIndex;
    root["profiles"]     = profilesArray;

    const QString path = configPath();
    if (QDir().mkpath(QFileInfo(path).absolutePath())) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(root).toJson());
    }
}

void InputProfileManager::load()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray profilesArray = root["profiles"].toArray();

    for (int i = 0; i < qMin(static_cast<int>(profilesArray.size()), MAX_PROFILES); ++i) {
        const QJsonObject obj = profilesArray[i].toObject();
        const QJsonObject bindings = obj["bindings"].toObject();

        for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
            if (m_profiles[i].bindings.contains(it.key()))
                m_profiles[i].bindings[it.key()] = it.value().toString();
        }
    }

    const int savedIndex = root["currentIndex"].toInt(0);
    if (savedIndex >= 0 && savedIndex < MAX_PROFILES)
        m_currentIndex = savedIndex;
}
