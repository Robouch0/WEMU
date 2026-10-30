#include <QGuiApplication>
#include <QKeyEvent>
#include <QTest>
#include <QWindow>
#include <gtest/gtest.h>
#include <vector>

#include "MenuKeyBridge.hpp"

namespace {
struct Key {
  int key;
  bool pressed;
  bool autoRepeat;
  bool operator==(const Key &) const = default;
};

class RecordingWindow : public QWindow {
public:
  std::vector<Key> keys;

protected:
  void keyPressEvent(QKeyEvent *event) override {
    keys.push_back({event->key(), true, event->isAutoRepeat()});
  }
  void keyReleaseEvent(QKeyEvent *event) override {
    keys.push_back({event->key(), false, event->isAutoRepeat()});
  }
};

class MenuKeyBridgeTest : public testing::Test {
protected:
  MenuKeyBridgeTest() : bridge([this] { return active; }) {
    window.show();
    bridge.setWindow(&window);
  }

  RecordingWindow window;
  bool active = true;
  MenuKeyBridge bridge;
};
} // namespace

TEST_F(MenuKeyBridgeTest, ButtonsBecomeKeys) {
  for (const auto *button :
       {"Up", "Down", "Left", "Right", "A", "B", "LB", "RB"})
    bridge.onButtonChanged(button, true);
  bridge.onButtonChanged("X", true);
  const std::vector<Key> expected{
      {Qt::Key_Up, true, false},     {Qt::Key_Down, true, false},
      {Qt::Key_Left, true, false},   {Qt::Key_Right, true, false},
      {Qt::Key_Space, true, false},  {Qt::Key_Escape, true, false},
      {Qt::Key_PageUp, true, false}, {Qt::Key_PageDown, true, false}};
  EXPECT_EQ(window.keys, expected);
}

TEST_F(MenuKeyBridgeTest, StickHasHysteresis) {
  bridge.onAxisChanged("LX", 0.4f);
  EXPECT_TRUE(window.keys.empty());
  bridge.onAxisChanged("LX", -0.6f);
  bridge.onAxisChanged("LX", -0.3f);
  ASSERT_EQ(window.keys.size(), 1u);
  bridge.onAxisChanged("LX", -0.1f);
  const std::vector<Key> expected{{Qt::Key_Left, true, false},
                                  {Qt::Key_Left, false, false}};
  EXPECT_EQ(window.keys, expected);
}

TEST_F(MenuKeyBridgeTest, HeldDirectionRepeats) {
  bridge.onButtonChanged("Down", true);
  QTest::qWait(600);
  bridge.onButtonChanged("Down", false);
  ASSERT_GE(window.keys.size(), 3u);
  EXPECT_TRUE(window.keys[1].autoRepeat);
  EXPECT_EQ(window.keys.back(), (Key{Qt::Key_Down, false, false}));
}

TEST_F(MenuKeyBridgeTest, HeldConfirmDoesNotRepeat) {
  bridge.onButtonChanged("A", true);
  QTest::qWait(450);
  EXPECT_EQ(window.keys.size(), 1u);
}

TEST_F(MenuKeyBridgeTest, DpadAndStickShareOnePress) {
  bridge.onButtonChanged("Left", true);
  bridge.onAxisChanged("LX", -1.0f);
  bridge.onButtonChanged("Left", false);
  EXPECT_EQ(window.keys.size(), 1u);
  bridge.onAxisChanged("LX", 0.0f);
  EXPECT_EQ(window.keys.back(), (Key{Qt::Key_Left, false, false}));
}

TEST_F(MenuKeyBridgeTest, IgnoresInputDuringAGame) {
  active = false;
  bridge.onButtonChanged("A", true);
  bridge.onButtonChanged("A", false);
  EXPECT_TRUE(window.keys.empty());
}

TEST_F(MenuKeyBridgeTest, ReleaseOfSuspendedPressIsDropped) {
  bridge.setSuspended(true);
  bridge.onButtonChanged("A", true);
  bridge.setSuspended(false);
  bridge.onButtonChanged("A", false);
  EXPECT_TRUE(window.keys.empty());
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
