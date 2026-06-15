import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import gui 1.0

Rectangle {
    color: "#e8e8ed"
    anchors.fill: parent

    property string listeningFor: ""

    Connections {
        target: InputManager

        function onButtonChanged(button, pressed, device) {
            if (!pressed) return

            if (listeningFor !== "") {
                if (InputProfileManager.isKnownXboxButton(button)) {
                    InputProfileManager.setBinding(listeningFor, button)
                    listeningFor = ""
                }
                return
            }

            // B → back to the main menu, mirroring the "← Home" button.
            if (button === "B")
                mainLoader.source = "pages/MainMenu.qml"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#ffffff"
            radius: 8
            border.color: "#d0d0d5"
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 16

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 110
                    height: 34
                    radius: 6
                    color: homeMouseArea.containsMouse ? "#d0d0d5" : "#e4e4ea"
                    Behavior on color { ColorAnimation { duration: 100 } }

                    Text {
                        anchors.centerIn: parent
                        text: "← Home"
                        color: "#1a1a1a"
                        font.pixelSize: 13
                        font.bold: true
                    }

                    MouseArea {
                        id: homeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mainLoader.source = "pages/MainMenu.qml"
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Input Settings"
                    color: "#1a1a1a"
                    font.pixelSize: 18
                    font.bold: true
                }
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

                    Row {
                        spacing: 6

                        Repeater {
                            model: 3

                            Rectangle {
                                width: 90
                                height: 28
                                radius: 4
                                color: InputProfileManager.currentProfileIndex === index ? "#3498ff" : "#e4e4ea"
                                border.color: InputProfileManager.currentProfileIndex === index ? "#2980d9" : "#cccccc"

                                Text {
                                    anchors.centerIn: parent
                                    text: "Profile " + (index + 1)
                                    font.pixelSize: 12
                                    color: InputProfileManager.currentProfileIndex === index ? "white" : "#444444"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        listeningFor = ""
                                        InputProfileManager.selectProfile(index)
                                    }
                                }
                            }
                        }

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
                            spacing: 5

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
                                model: InputProfileManager.bindingModel

                                BindingRow {
                                    width: parent.width
                                    wiiuLabel: modelData.wiiuLabel
                                    xboxButton: modelData.xboxButton
                                    isListening: listeningFor === modelData.wiiu
                                    onRowClicked: listeningFor = modelData.wiiu
                                }
                            }
                        }
                    }

                    Item {
                        width: parent.width - 24
                        height: 26

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            visible: listeningFor !== ""
                            height: 26
                            width: listeningText.implicitWidth + 28
                            radius: 13
                            color: "#f5a623"

                            SequentialAnimation on opacity {
                                running: listeningFor !== ""
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.55; duration: 600; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0;  duration: 600; easing.type: Easing.InOutQuad }
                            }

                            Text {
                                id: listeningText
                                anchors.centerIn: parent
                                text: "● Press a button on the controller…"
                                color: "white"
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            visible: listeningFor === ""
                            text: InputManager.connectedDevices().length > 0
                                  ? "Connected: " + InputManager.connectedDevices().join(", ")
                                  : "No controller connected"
                            color: "#888888"
                            font.pixelSize: 11
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: resetText.implicitWidth + 18
                            height: 22
                            radius: 4
                            color: resetMouseArea.containsMouse ? "#f3d6d3" : "#e4e4ea"
                            border.color: "#cccccc"
                            Behavior on color { ColorAnimation { duration: 100 } }

                            Text {
                                id: resetText
                                anchors.centerIn: parent
                                text: "Reset to default"
                                font.pixelSize: 10
                                font.bold: true
                                color: "#c0392b"
                            }

                            MouseArea {
                                id: resetMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    listeningFor = ""
                                    InputProfileManager.resetCurrentProfile()
                                }
                            }
                        }
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
