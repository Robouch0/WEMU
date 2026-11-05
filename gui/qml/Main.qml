import QtQuick
import QtQuick.Controls
import gui 1.0
import "pages"

ApplicationWindow {
    id: rootWindow
    visibility: AppPreferences.fullscreen ? Window.FullScreen : Window.Windowed
    width: 1280
    height: 720
    minimumWidth: 800
    minimumHeight: 640
    color: Theme.bg
    title: EmulatorLauncher.running ? "WEMU — " + EmulatorLauncher.gameTitle : "WEMU Library"

    onActiveFocusItemChanged: MenuAudio.play("move")

    // Survive the page swap between the library and Settings.
    property int libraryIndex: 0
    property int settingsTab: 0
    property bool startupSelectionDone: false
    // The library loads under the intro, which hands over to it as it ends.
    // introPlaying: the intro has the keys and the logo. introVisible: it still covers the window.
    property bool introPlaying: true
    property bool introVisible: true

    Shortcut {
        sequence: "F11"
        onActivated: AppPreferences.fullscreen = !AppPreferences.fullscreen
    }

    // Pages swap by setting this loader's source.
    Loader {
        id: mainLoader
        anchors.fill: parent
        visible: !EmulatorLauncher.running
        enabled: visible
        source: "pages/MainMenu.qml"
    }

    Loader {
        anchors.fill: parent
        active: rootWindow.introVisible
        source: "pages/IntroSequence.qml"
    }

    GameSessionView { anchors.fill: parent }
}
