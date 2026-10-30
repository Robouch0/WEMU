#include "AppPreferences.hpp"
#include <QSettings>

bool AppPreferences::fullscreen() const { return QSettings().value("display/fullscreen", false).toBool(); }

void AppPreferences::setFullscreen(bool fullscreen)
{
    QSettings().setValue("display/fullscreen", fullscreen);
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
