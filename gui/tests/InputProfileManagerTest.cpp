#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <gtest/gtest.h>

#include "InputProfileManager.hpp"

namespace {
class InputProfileManagerTest : public testing::Test {
protected:
    void SetUp() override
    {
        QStandardPaths::setTestModeEnabled(true);
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        for (const auto &name : QDir(directory).entryList(QDir::Files)) QFile::remove(directory + "/" + name);
    }
};
}

TEST_F(InputProfileManagerTest, BrowsingDoesNotChangeTheProfileGamesUse)
{
    InputProfileManager profiles;
    profiles.setBinding("A", "B");
    const auto active = profiles.bindings();

    profiles.editProfile(1);
    profiles.setBinding("A", "X");

    EXPECT_EQ(profiles.currentProfileIndex(), 0);
    EXPECT_EQ(profiles.editedProfileIndex(), 1);
    EXPECT_EQ(profiles.bindings(), active);
    EXPECT_EQ(profiles.getBinding("A"), "X");
}

TEST_F(InputProfileManagerTest, UsingAProfileChangesGamesAndPersists)
{
    {
        InputProfileManager profiles;
        profiles.editProfile(2);
        profiles.setBinding("A", "Y");
        profiles.selectProfile(2);
        EXPECT_EQ(profiles.bindings().value("A"), "Y");
    }
    InputProfileManager reloaded;
    EXPECT_EQ(reloaded.currentProfileIndex(), 2);
    EXPECT_EQ(reloaded.editedProfileIndex(), 2);
    EXPECT_EQ(reloaded.bindings().value("A"), "Y");
}

TEST_F(InputProfileManagerTest, ResetOnlyTouchesTheViewedProfile)
{
    InputProfileManager profiles;
    profiles.setBinding("A", "B");
    profiles.editProfile(1);
    profiles.setBinding("A", "X");
    profiles.resetEditedProfile();

    EXPECT_EQ(profiles.getBinding("A"), "A");
    EXPECT_EQ(profiles.bindings().value("A"), "B");
}
