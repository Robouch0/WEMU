#include "AppLanguage.hpp"
#include <QCoreApplication>
#include <QSettings>
#include <QVariantMap>

namespace {
const auto *settingsKey = "ui/language";

struct Language {
    const char *code;
    const char *name;
};

// English is the source text, so it has no translation file. Names stay in their own language
// so anyone can find theirs, whatever language is showing.
const Language supported[] = {{"en", "English"}, {"fr", "Français"}, {"es", "Español"}, {"ko", "한국어"}};

bool isSupported(const QString &code)
{
    for (const auto &language : supported)
        if (code == QLatin1String(language.code)) return true;
    return false;
}
}

AppLanguage::AppLanguage(QObject *parent) : QObject(parent)
{
    const auto saved = QSettings().value(settingsKey).toString();
    apply(isSupported(saved) ? saved : closestSupported(QLocale::system()));
}

QVariantList AppLanguage::languages()
{
    QVariantList result;
    for (const auto &language : supported)
        result.append(QVariantMap{{"code", QString::fromLatin1(language.code)}, {"name", QString::fromUtf8(language.name)}});
    return result;
}

QString AppLanguage::closestSupported(const QLocale &locale)
{
    const auto code = locale.name().section('_', 0, 0);
    return isSupported(code) ? code : QStringLiteral("en");
}

void AppLanguage::select(const QString &code)
{
    if (code == m_code || !isSupported(code)) return;
    QSettings().setValue(settingsKey, code);
    apply(code);
}

void AppLanguage::apply(const QString &code)
{
    QCoreApplication::removeTranslator(&m_translator);
    if (code != QLatin1String("en") && m_translator.load(":/i18n/wemu_" + code))
        QCoreApplication::installTranslator(&m_translator);
    m_code = code;
    emit codeChanged();
}
