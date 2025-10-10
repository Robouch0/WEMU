import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 400
    height: 200
    title: qsTr("Emulator Counter")

    Column {
        anchors.centerIn: parent
        spacing: 20

        Text {
            text: "Seconds: " + emulatorCounter.seconds
            font.pixelSize: 30
        }

        Button {
            text: "Reset"
            onClicked: emulatorCounter.reset()
        }
    }
}
