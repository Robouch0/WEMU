#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include "InputProfile.hpp"

class InputProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bindingModel READ bindingModel NOTIFY bindingsChanged)
    Q_PROPERTY(int currentProfileIndex READ currentProfileIndex NOTIFY bindingsChanged)

public:
    explicit InputProfileManager(QObject *parent = nullptr);

    [[nodiscard]] QVariantList bindingModel() const;
    [[nodiscard]] int currentProfileIndex() const;

    Q_INVOKABLE void setBinding(const QString &wiiuButton, const QString &xboxButton);
    Q_INVOKABLE [[nodiscard]] QString getBinding(const QString &wiiuButton) const;
    Q_INVOKABLE void selectProfile(int index);

signals:
    void bindingsChanged();

private:
    void save() const;
    void load();

    [[nodiscard]] InputProfile createDefaultProfile(const QString &name) const;
    [[nodiscard]] static QString configPath();

    QList<InputProfile> m_profiles;
    int m_currentIndex = 0;

    static const QStringList s_displayOrder;
    static constexpr int MAX_PROFILES = 3;
};
