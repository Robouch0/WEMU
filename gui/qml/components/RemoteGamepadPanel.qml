import QtQuick
import QtQuick.Layouts
import gui 1.0

// Mobile GamePad pairing and status: the PIN a phone enters, where to reach this PC, and which
// phones are remembered.
SettingsTabPage {
    SettingsCard {
        Layout.fillWidth: true

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            PhoneGlyph { connected: RemoteGamepad.clientConnected }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "Mobile GamePad"
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontBody
                    color: RemoteGamepad.error !== "" ? Theme.danger
                         : RemoteGamepad.clientConnected ? Theme.success : Theme.textMuted
                    text: !RemoteGamepad.enabled ? "Turned off"
                          : RemoteGamepad.error !== "" ? RemoteGamepad.error
                          : RemoteGamepad.clientConnected ? "Connected · " + RemoteGamepad.clientName + (RemoteGamepad.streaming ? " · streaming" : "")
                          : "Waiting for a phone on Wi-Fi…"
                }
            }

            MenuSwitch {
                checked: RemoteGamepad.enabled
                onToggled: RemoteGamepad.enabled = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: RemoteGamepad.clientConnected
            spacing: Theme.space3
            Text {
                Layout.fillWidth: true
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                text: "Games launched here or from the phone show the GamePad screen on " + RemoteGamepad.clientName + "."
            }
            MenuButton {
                text: "Disconnect"
                onClicked: RemoteGamepad.disconnectClient()
            }
        }
    }

    SettingsCard {
        Layout.fillWidth: true
        visible: RemoteGamepad.enabled && !RemoteGamepad.clientConnected
        title: "Pair a phone"

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontBody
            color: Theme.textMuted
            text: "Open the WEMU GamePad app on a phone connected to the same Wi-Fi, pick this PC, then enter:"
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.space2
            Repeater {
                model: RemoteGamepad.pin.split("")
                Rectangle {
                    width: 52
                    height: 64
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.color: Theme.accent
                    border.width: 2
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                        font.family: "monospace"
                        color: Theme.text
                    }
                }
            }
            MenuButton {
                Layout.leftMargin: Theme.space3
                text: "New PIN"
                onClicked: RemoteGamepad.regeneratePin()
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            font.family: "monospace"
            wrapMode: Text.WrapAnywhere
            text: RemoteGamepad.addresses.length === 0 ? "No network connection"
                  : RemoteGamepad.addresses.map(a => a + ":" + RemoteGamepad.port).join("   ")
        }
    }

    SettingsCard {
        Layout.fillWidth: true
        visible: RemoteGamepad.pairedDevices.length > 0
        title: "Paired phones"

        Repeater {
            model: RemoteGamepad.pairedDevices
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                    text: modelData.name + (modelData.platform ? "  (" + modelData.platform + ")" : "")
                }
                MenuButton {
                    text: "Forget"
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
            border.color: connected ? Theme.success : Theme.accent
            border.width: 2.5
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            width: 8
            height: 2.5
            radius: 1
            color: connected ? Theme.success : Theme.accent
        }
    }
}
