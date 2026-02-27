import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import gui 1.0

Rectangle {
    color: "#e8e8ed"
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Rectangle {
            width: parent.width
            height: 100
            color: "#dddddd"
            radius: 8

            Row {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 16

                Button { text: "← Home"; width: 120 }
                Button { text: "Input" }
                Button { text: "Graphics" }
                Button { text: "Audio" }
            }
        }

        RowLayout {
            id: mainRow
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 20

            Rectangle {
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                Layout.fillHeight: true

                color: "#f5f5f8"
                radius: 10
                border.color: "#d0d0d5"

                Column {
                    width: parent.width
                    spacing: 12
                    padding: 12

                    Text {
                        text: "Physical Controller"
                        font.pixelSize: 20
                        color: "#1a1a1a"
                    }

                    ControllerPreview {
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 4
                Layout.fillWidth: true
                Layout.fillHeight: true

                color: "#f5f5f8"
                radius: 10
                border.color: "#d0d0d5"

                Column {
                    width: parent.width
                    spacing: 12
                    padding: 12

                    Text {
                        text: "Mapping"
                        font.pixelSize: 20
                        color: "#1a1a1a"
                    }

                    Rectangle {
                        width: parent.width - 24
                        height: 300
                        color: "#ffffff"
                        radius: 6
                        border.color: "#cccccc"
                    }

                    Text {
                        text: "Devices: " + InputManager.connectedDevices().join(", ")
                        color: "#555555"
                    }

                    Text {
                        id: keyLabel
                        text: "Waiting for input..."
                        color: "#c46a00"
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                Layout.fillHeight: true

                color: "#f5f5f8"
                radius: 10
                border.color: "#d0d0d5"

                Column {
                    width: parent.width
                    spacing: 12
                    padding: 12

                    Text {
                        text: "Virtual Gamepad Controller"
                        font.pixelSize: 20
                        color: "#1a1a1a"
                    }

                    GamepadPreview {
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
    }
}
