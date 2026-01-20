import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1450
    height: 950
    title: "Emulator"

    Loader {
        id: mainLoader
        anchors.fill: parent
        sourceComponent: introComponent
    }

    Component {
        id: introComponent
        Loader {
            anchors.fill: parent
            source: "pages/IntroSequence.qml"
            onLoaded: {
                item.introFinished.connect(() => {
                    console.log("Intro finished → loading main menu")
                    mainLoader.source = "pages/MainMenu.qml"
                })
            }
        }
    }
}
