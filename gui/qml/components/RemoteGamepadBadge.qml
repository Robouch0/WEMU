import QtQuick
import gui 1.0

// Header button: mobile GamePad status at a glance. The pairing details live in Settings.
MenuButton {
    id: badge

    readonly property bool connected: RemoteGamepad.clientConnected

    visible: RemoteGamepad.enabled
    textLeftInset: Theme.space4
    text: connected ? "GamePad: " + RemoteGamepad.clientName : "Pair phone · " + RemoteGamepad.pin

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space4
        anchors.verticalCenter: parent.verticalCenter
        width: 10
        height: 10
        radius: 5
        color: badge.connected ? Theme.success : Theme.warning
    }
}
