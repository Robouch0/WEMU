import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import gui 1.0

Rectangle {
    color: "#e8e8ed"
    anchors.fill: parent

    property var bindingModel: [
        { wiiu: "A",          wiiuLabel: "A",       xboxButton: "A" },
        { wiiu: "B",          wiiuLabel: "B",       xboxButton: "B" },
        { wiiu: "X",          wiiuLabel: "X",       xboxButton: "X" },
        { wiiu: "Y",          wiiuLabel: "Y",       xboxButton: "Y" },
        { wiiu: "DPAD_UP",    wiiuLabel: "D-Up",    xboxButton: "DPAD_UP" },
        { wiiu: "DPAD_DOWN",  wiiuLabel: "D-Down",  xboxButton: "DPAD_DOWN" },
        { wiiu: "DPAD_LEFT",  wiiuLabel: "D-Left",  xboxButton: "DPAD_LEFT" },
        { wiiu: "DPAD_RIGHT", wiiuLabel: "D-Right", xboxButton: "DPAD_RIGHT" },
        { wiiu: "L",          wiiuLabel: "L",       xboxButton: "LB" },
        { wiiu: "R",          wiiuLabel: "R",       xboxButton: "RB" },
        { wiiu: "ZL",         wiiuLabel: "ZL",      xboxButton: "LT" },
        { wiiu: "ZR",         wiiuLabel: "ZR",      xboxButton: "RT" },
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#dddddd"
            radius: 8

            Row {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12

                Button {
                    text: "← Home"
                    width: 120
                    onClicked: mainLoader.source = "pages/MainMenu.qml"
                }
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
                    spacing: 25
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
                        height: 28
                        color: "#e4e4ea"
                        radius: 4

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 0

                            Text {
                                Layout.fillWidth: true
                                text: "Xbox"
                                font.pixelSize: 12
                                font.bold: true
                                color: "#444444"
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Wii U"
                                font.pixelSize: 12
                                font.bold: true
                                color: "#444444"
                                leftPadding: 8
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width - 24
                        height: bindingRepeater.count * 36
                        color: "#ffffff"
                        radius: 6
                        border.color: "#cccccc"
                        clip: true

                        Column {
                            anchors.fill: parent

                            Repeater {
                                id: bindingRepeater
                                model: bindingModel

                                BindingRow {
                                    width: parent.width
                                    wiiuLabel: modelData.wiiuLabel
                                    xboxButton: modelData.xboxButton
                                    isListening: false
                                }
                            }
                        }
                    }

                    Text {
                        text: "Devices: " + InputManager.connectedDevices().join(", ")
                        color: "#555555"
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
                    spacing: 25
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
