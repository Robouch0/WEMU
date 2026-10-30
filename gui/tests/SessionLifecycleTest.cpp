#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QSettings>
#include <QThread>
#include <gtest/gtest.h>

#include "EmulatorLauncher.hpp"
#include "LocaleSettings.hpp"

TEST(SessionSettingsTest, RegionMetadataIsParsedAsHexAndMalformedFilesFallBack)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir().mkpath(directory.path() + "/code"));
    ASSERT_TRUE(QDir().mkpath(directory.path() + "/meta"));
    const auto rpx = directory.path() + "/code/game.rpx";
    EXPECT_EQ(titleRegionForLaunch(rpx), 4u);
    QFile meta(directory.path() + "/meta/meta.xml");
    for (const auto &test: {std::pair{"<menu><region>00000002</region></menu>", 2u},
                            std::pair{"<menu><region>00000001</region></menu>", 1u},
                            std::pair{"<menu><region>00000010</region></menu>", 16u},
                            std::pair{"<menu><region>0000007F</region></menu>", 4u},
                            std::pair{"<menu><region>bad-value</region></menu>", 4u},
                            std::pair{"<menu><region>00000002</region>", 4u}}) {
        ASSERT_TRUE(meta.open(QIODevice::WriteOnly | QIODevice::Truncate));
        meta.write(test.first);
        meta.close();
        EXPECT_EQ(titleRegionForLaunch(rpx), test.second);
    }
}

TEST(SessionSettingsTest, LanguageAndRegionPersistAndRejectInvalidSelections)
{
    QSettings settings;
    settings.remove("emulation/systemLanguage");
    settings.remove("emulation/systemRegion");
    EmulatorLauncher first;
    EXPECT_EQ(first.systemLanguage(), 1);
    EXPECT_EQ(first.systemRegion(), 0);
    first.setSystemLanguage(2);
    first.setSystemRegion(2);
    first.setSystemLanguage(-1);
    first.setSystemRegion(3);
    EmulatorLauncher second;
    EXPECT_EQ(second.systemLanguage(), 2);
    EXPECT_EQ(second.systemRegion(), 2);
    settings.setValue("emulation/systemLanguage", 999);
    settings.setValue("emulation/systemRegion", -1);
    EXPECT_EQ(second.systemLanguage(), 1);
    EXPECT_EQ(second.systemRegion(), 0);
    settings.setValue("emulation/systemLanguage", "not-a-language");
    EXPECT_EQ(second.systemLanguage(), 1);
    settings.remove("emulation/systemLanguage");
    settings.remove("emulation/systemRegion");
}

TEST(SessionSettingsTest, AudioVolumePersistsClampsAndHandlesMalformedSettings)
{
    QSettings settings;
    settings.remove("audio/volume");
    EmulatorLauncher first;
    EXPECT_EQ(first.audioVolume(), 100);
    first.setAudioVolume(37);
    EmulatorLauncher second;
    EXPECT_EQ(second.audioVolume(), 37);
    second.setAudioVolume(-1);
    EXPECT_EQ(first.audioVolume(), 0);
    second.setAudioVolume(200);
    EXPECT_EQ(first.audioVolume(), 100);
    settings.setValue("audio/volume", "invalid");
    EXPECT_EQ(first.audioVolume(), 100);
    settings.remove("audio/volume");
}

TEST(SessionLifecycleTest, MissingFilesKeepTheLibraryAvailable)
{
    EmulatorLauncher launcher;
    launcher.launch("/missing/wemu-test.rpx", "Missing", "/missing/content");
    EXPECT_FALSE(launcher.running());
    EXPECT_FALSE(launcher.connected());
    EXPECT_FALSE(launcher.error().isEmpty());
}

TEST(SessionLifecycleTest, CoreStartupFailureClearsSessionAndAllowsAnotherLaunch)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir().mkpath(directory.path() + "/content"));
    const auto rpx = directory.path() + "/invalid.rpx";
    QFile file(rpx);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("This is deliberately not an RPX");
    file.close();
    EmulatorLauncher launcher;
    unsigned launches{}, returns{};
    QObject::connect(&launcher, &EmulatorLauncher::stateChanged, [&](bool running) { running ? ++launches : ++returns; });
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        launcher.launch(rpx, "Invalid test title", directory.path() + "/content");
        QElapsedTimer timer;
        timer.start();
        do {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        } while ((launcher.running() || launcher.error().isEmpty()) && timer.elapsed() < 10000);
        EXPECT_FALSE(launcher.running());
        EXPECT_FALSE(launcher.connected());
        EXPECT_FALSE(launcher.hasFrame());
        EXPECT_FALSE(launcher.paused());
        EXPECT_FALSE(launcher.error().isEmpty());
    }
    EXPECT_GE(launches, 2u);
    EXPECT_GE(returns, 2u);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("WemuSessionTests");
    app.setApplicationName("SessionLifecycle");
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
