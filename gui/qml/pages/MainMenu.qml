import QtQuick
import QtQuick.Controls

Rectangle {
    color: "black"
    anchors.fill: parent

    Column {
        anchors.centerIn: parent
        spacing: 20

        Text {
            text: "Main Menu"
            color: "green"
            font.pixelSize: 40
            horizontalAlignment: Text.AlignHCenter
        }

        Button {
            text: "Settings"
            onClicked: {
                // Navigate to settings page
                mainLoader.source = "pages/SettingsPage.qml"
            }
        }
    }
}
