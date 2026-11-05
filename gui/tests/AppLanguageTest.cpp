#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "AppLanguage.hpp"

TEST(AppLanguageTest, FollowsSupportedSystemLanguages)
{
    EXPECT_EQ(AppLanguage::closestSupported(QLocale("fr_CA")), "fr");
    EXPECT_EQ(AppLanguage::closestSupported(QLocale("es_MX")), "es");
    EXPECT_EQ(AppLanguage::closestSupported(QLocale("ko_KR")), "ko");
}

TEST(AppLanguageTest, FallsBackToEnglish)
{
    EXPECT_EQ(AppLanguage::closestSupported(QLocale("de_DE")), "en");
    EXPECT_EQ(AppLanguage::closestSupported(QLocale::c()), "en");
}

TEST(AppLanguageTest, RemembersTheChoice)
{
    QSettings().remove("ui/language");
    {
        AppLanguage language;
        language.select("ko");
        EXPECT_EQ(language.code(), "ko");
    }
    AppLanguage reloaded;
    EXPECT_EQ(reloaded.code(), "ko");
}

TEST(AppLanguageTest, IgnoresUnknownLanguages)
{
    AppLanguage language;
    const auto before = language.code();
    language.select("xx");
    EXPECT_EQ(language.code(), before);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir settings;
    QCoreApplication::setOrganizationName("WEMU-tests");
    QCoreApplication::setApplicationName("language");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
