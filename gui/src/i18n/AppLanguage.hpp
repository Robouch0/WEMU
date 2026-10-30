#pragma once
#include <QLocale>
#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVariantList>

// The language of WEMU's own text. A game's language is a separate console setting.
class AppLanguage : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString code READ code NOTIFY codeChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)

public:
    explicit AppLanguage(QObject *parent = nullptr);

    [[nodiscard]] QString code() const { return m_code; }
    [[nodiscard]] static QVariantList languages();
    // The supported language closest to a locale, or English.
    [[nodiscard]] static QString closestSupported(const QLocale &locale);

    Q_INVOKABLE void select(const QString &code);

signals:
    void codeChanged();

private:
    void apply(const QString &code);

    QString m_code;
    QTranslator m_translator;
};
