import QtQuick
import QtQuick.Controls
import "pages"

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1280
    height: 720
    minimumWidth: 800
    minimumHeight: 640
    title: EmulatorLauncher.running ? "WEMU — " + EmulatorLauncher.gameTitle : "WEMU Library"

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
