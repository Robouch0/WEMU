import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1200
    height: 900
    title: "Emulator"

    Keys.onPressed: InputManager.keyPressed(key, event.text)
    Keys.onReleased: InputManager.keyReleased(key, event.text)

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
