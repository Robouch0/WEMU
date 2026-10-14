import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Mobile GamePad pairing and status: the PIN a phone enters, where to reach this PC, and which
// phones are remembered.
Rectangle {
    id: panel
    implicitWidth: 420
    implicitHeight: content.implicitHeight + 32
    radius: 12
    color: "#ffffff"
    border.color: "#d0d0d5"

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            PhoneGlyph { connected: RemoteGamepad.clientConnected }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "Mobile GamePad"
                    font.pixelSize: 16
                    font.bold: true
                    color: "#1a1a1a"
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: RemoteGamepad.clientConnected ? "#1e9e5a" : "#666666"
                    text: !RemoteGamepad.enabled ? "Off"
                          : RemoteGamepad.error !== "" ? RemoteGamepad.error
                          : RemoteGamepad.clientConnected ? "Connected · " + RemoteGamepad.clientName + (RemoteGamepad.streaming ? " · streaming" : "")
                          : "Waiting for a phone on Wi-Fi…"
                }
            }

            Switch {
                checked: RemoteGamepad.enabled
                onToggled: RemoteGamepad.enabled = checked
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: RemoteGamepad.enabled && !RemoteGamepad.clientConnected
            implicitHeight: pairing.implicitHeight + 24
            radius: 10
            color: "#f3f6fb"
            border.color: "#dce4f0"

            ColumnLayout {
                id: pairing
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#444444"
                    text: "Open the WEMU GamePad app on a phone connected to the same Wi-Fi, pick this PC, then enter:"
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    Repeater {
                        model: RemoteGamepad.pin.split("")
                        Rectangle {
                            width: 44
                            height: 54
                            radius: 8
                            color: "#ffffff"
                            border.color: "#3498ff"
                            border.width: 2
                            Label {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 28
                                font.bold: true
                                font.family: "monospace"
                                color: "#1a1a1a"
                            }
                        }
                    }
                    ToolButton {
                        text: "↻"
                        font.pixelSize: 18
                        ToolTip.visible: hovered
                        ToolTip.text: "New PIN"
                        onClicked: RemoteGamepad.regeneratePin()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    color: "#666666"
                    font.family: "monospace"
                    text: RemoteGamepad.addresses.length === 0 ? "No network connection"
                          : RemoteGamepad.addresses.map(a => a + ":" + RemoteGamepad.port).join("   ")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: RemoteGamepad.clientConnected
            Label {
                Layout.fillWidth: true
                color: "#444444"
                wrapMode: Text.WordWrap
                text: "Games launched here or from the phone show the GamePad screen on " + RemoteGamepad.clientName + "."
            }
            Button {
                text: "Disconnect"
                onClicked: RemoteGamepad.disconnectClient()
            }
        }

        Label {
            visible: RemoteGamepad.pairedDevices.length > 0
            text: "Paired phones"
            font.bold: true
            color: "#1a1a1a"
        }

        Repeater {
            model: RemoteGamepad.pairedDevices
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: "#333333"
                    text: modelData.name + (modelData.platform ? "  (" + modelData.platform + ")" : "")
                }
                Button {
                    text: "Forget"
                    flat: true
                    onClicked: RemoteGamepad.forgetDevice(modelData.id)
                }
            }
        }
    }

    component PhoneGlyph: Item {
        property bool connected
        width: 28
        height: 36
        Rectangle {
            anchors.fill: parent
            radius: 5
            color: "transparent"
            border.color: connected ? "#1e9e5a" : "#3498ff"
            border.width: 2.5
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            width: 8
            height: 2.5
            radius: 1
            color: connected ? "#1e9e5a" : "#3498ff"
        }
    }
}
