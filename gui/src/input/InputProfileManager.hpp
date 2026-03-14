#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include "InputProfile.hpp"

class InputProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bindingModel READ bindingModel NOTIFY bindingsChanged)

public:
    explicit InputProfileManager(QObject *parent = nullptr);

    [[nodiscard]] QVariantList bindingModel() const;

    Q_INVOKABLE void setBinding(const QString &wiiuButton, const QString &xboxButton);
    Q_INVOKABLE [[nodiscard]] QString getBinding(const QString &wiiuButton) const;

signals:
    void bindingsChanged();

private:
    void save() const;
    void load();

    [[nodiscard]] static QString configPath();

    InputProfile m_profile;

    static const QStringList s_displayOrder;
};
