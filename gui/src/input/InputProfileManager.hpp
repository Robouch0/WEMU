#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include "InputProfile.hpp"

class InputProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bindingModel READ bindingModel NOTIFY bindingsChanged)
    Q_PROPERTY(int currentProfileIndex READ currentProfileIndex NOTIFY bindingsChanged)
    Q_PROPERTY(int editedProfileIndex READ editedProfileIndex NOTIFY bindingsChanged)

    public:
        explicit InputProfileManager(QObject *parent = nullptr);

        [[nodiscard]] QVariantList bindingModel() const;
        // The profile games use, kept apart from the one open in Settings.
        [[nodiscard]] int currentProfileIndex() const;
        [[nodiscard]] int editedProfileIndex() const { return m_editedIndex; }
        // The active profile: Wii U button -> controller button.
        [[nodiscard]] const QMap<QString, QString> &bindings() const { return m_profiles[m_currentIndex].bindings; }

        Q_INVOKABLE void setBinding(const QString &wiiuButton, const QString &xboxButton);
        Q_INVOKABLE [[nodiscard]] QString getBinding(const QString &wiiuButton) const;
        Q_INVOKABLE [[nodiscard]] bool isKnownXboxButton(const QString &button) const;
        Q_INVOKABLE void selectProfile(int index);
        Q_INVOKABLE void editProfile(int index);
        Q_INVOKABLE void resetEditedProfile();

    signals:
        void bindingsChanged();

    private:
        void save() const;
        void load();

        [[nodiscard]] static InputProfile createDefaultProfile(const QString &name);
        [[nodiscard]] static QString configPath();

        QList<InputProfile> m_profiles;
        int m_currentIndex = 0;
        int m_editedIndex = 0;

        static const QStringList s_displayOrder;
        static constexpr int k_maxProfiles = 3;
};
