#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include "EmulatorLauncher.hpp"

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
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
