#pragma once
#include <QObject>
#include <QString>

// Saved preferences for WEMU itself rather than for an emulated console.
class AppPreferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY changed)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY changed)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY changed)
    Q_PROPERTY(bool rememberLastGame READ rememberLastGame WRITE setRememberLastGame NOTIFY changed)
    Q_PROPERTY(QString lastGame READ lastGame WRITE setLastGame NOTIFY changed)

public:
    using QObject::QObject;

    [[nodiscard]] bool fullscreen() const;
    void setFullscreen(bool fullscreen);
    [[nodiscard]] QString themeMode() const;
    void setThemeMode(const QString &mode);
    [[nodiscard]] QString accentColor() const;
    void setAccentColor(const QString &color);
    [[nodiscard]] bool rememberLastGame() const;
    void setRememberLastGame(bool remember);
    [[nodiscard]] QString lastGame() const;
    void setLastGame(const QString &rpxPath);

signals:
    void changed();
};
