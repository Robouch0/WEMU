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

    Shortcut {
        sequence: "F11"
        onActivated: AppPreferences.fullscreen = !AppPreferences.fullscreen
    }

    Loader {
        id: mainLoader
        anchors.fill: parent
        visible: !EmulatorLauncher.running
        enabled: visible
        sourceComponent: introComponent
    }

    GameSessionView { anchors.fill: parent }

    Component {
        id: introComponent
        Loader {
            anchors.fill: parent
            source: "pages/MainMenu.qml"
            // source: "pages/IntroSequence.qml"
            // onLoaded: {
            //     item.introFinished.connect(() => {
            //         console.log("Intro finished → loading main menu")
            //         mainLoader.source = "pages/MainMenu.qml"
            //     })
            // }
        }
    }
}
