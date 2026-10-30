#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "AppPreferences.hpp"

TEST(AppPreferencesTest, HasSafeDefaults)
{
    QSettings().clear();
    const AppPreferences preferences;
    EXPECT_FALSE(preferences.fullscreen());
    EXPECT_TRUE(preferences.rememberLastGame());
    EXPECT_TRUE(preferences.lastGame().isEmpty());
}

TEST(AppPreferencesTest, KeepsChangesAcrossInstances)
{
    QSettings().clear();
    {
        AppPreferences preferences;
        preferences.setFullscreen(true);
        preferences.setRememberLastGame(false);
        preferences.setLastGame("/games/pong/code/pong.rpx");
    }
    const AppPreferences reloaded;
    EXPECT_TRUE(reloaded.fullscreen());
    EXPECT_FALSE(reloaded.rememberLastGame());
    EXPECT_EQ(reloaded.lastGame(), "/games/pong/code/pong.rpx");
}

TEST(AppPreferencesTest, ThemeDefaultsToLightBlue)
{
    QSettings().clear();
    const AppPreferences preferences;
    EXPECT_EQ(preferences.themeMode(), "light");
    EXPECT_EQ(preferences.accentColor(), "blue");
}

TEST(AppPreferencesTest, KeepsTheChosenTheme)
{
    QSettings().clear();
    {
        AppPreferences preferences;
        preferences.setThemeMode("dark");
        preferences.setAccentColor("green");
    }
    const AppPreferences reloaded;
    EXPECT_EQ(reloaded.themeMode(), "dark");
    EXPECT_EQ(reloaded.accentColor(), "green");
}

TEST(AppPreferencesTest, AcceptsEveryAccent)
{
    AppPreferences preferences;
    for (const auto *color : {"blue", "teal", "green", "amber", "orange", "red", "pink", "purple"}) {
        preferences.setAccentColor(color);
        EXPECT_EQ(preferences.accentColor(), color);
    }
}

TEST(AppPreferencesTest, IgnoresUnknownThemes)
{
    QSettings().clear();
    AppPreferences preferences;
    preferences.setThemeMode("sepia");
    preferences.setAccentColor("chartreuse");
    EXPECT_EQ(preferences.themeMode(), "light");
    EXPECT_EQ(preferences.accentColor(), "blue");

    QSettings().setValue("appearance/accent", "chartreuse");
    EXPECT_EQ(preferences.accentColor(), "blue");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir settings;
    QCoreApplication::setOrganizationName("WEMU-tests");
    QCoreApplication::setApplicationName("preferences");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
