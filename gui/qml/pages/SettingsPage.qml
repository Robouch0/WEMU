import QtQuick
import QtQuick.Controls

Page {
    id: settingsPage
    anchors.fill: parent
    background: Rectangle { color: "black" }

    Column {
        anchors.centerIn: parent
        spacing: 15

        Text {
            text: "Keyboard Layout: " + InputManager.keyboardLayout
            color: "white"
            font.pixelSize: 18
        }

        Text {
            id: keyLabel
            text: "Press any key..."
            color: "lime"
            font.pixelSize: 22
        }

        Connections {
            target: InputManager
            function onKeyChanged(keyText, pressed) {
                keyLabel.text = (pressed ? "Key pressed: " : "Key released: ") + keyText;
            }
        }

        Button {
            text: "Back to Menu"
            onClicked: mainLoader.source = "pages/MainMenu.qml"
        }
    }
}
