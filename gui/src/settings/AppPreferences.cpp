#include "AppPreferences.hpp"
#include <QSettings>
#include <QStringList>

namespace {
const QStringList accentColors{"blue", "teal", "green", "amber", "orange", "red", "pink", "purple"};
}

bool AppPreferences::fullscreen() const { return QSettings().value("display/fullscreen", false).toBool(); }

void AppPreferences::setFullscreen(bool fullscreen)
{
    QSettings().setValue("display/fullscreen", fullscreen);
    emit changed();
}

QString AppPreferences::themeMode() const
{
    const auto mode = QSettings().value("appearance/mode").toString();
    return mode == QLatin1String("dark") ? mode : QStringLiteral("light");
}

void AppPreferences::setThemeMode(const QString &mode)
{
    if (mode != QLatin1String("light") && mode != QLatin1String("dark")) return;
    QSettings().setValue("appearance/mode", mode);
    emit changed();
}

QString AppPreferences::accentColor() const
{
    const auto color = QSettings().value("appearance/accent").toString();
    return accentColors.contains(color) ? color : QStringLiteral("blue");
}

void AppPreferences::setAccentColor(const QString &color)
{
    if (!accentColors.contains(color)) return;
    QSettings().setValue("appearance/accent", color);
    emit changed();
}

bool AppPreferences::rememberLastGame() const { return QSettings().value("library/rememberLastGame", true).toBool(); }

void AppPreferences::setRememberLastGame(bool remember)
{
    QSettings().setValue("library/rememberLastGame", remember);
    emit changed();
}

QString AppPreferences::lastGame() const { return QSettings().value("library/lastGame").toString(); }

void AppPreferences::setLastGame(const QString &rpxPath)
{
    QSettings().setValue("library/lastGame", rpxPath);
    emit changed();
}
