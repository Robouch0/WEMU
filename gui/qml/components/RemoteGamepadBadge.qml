import QtQuick
import gui 1.0

// Header button: mobile GamePad status at a glance. The pairing details live in Settings.
MenuButton {
    readonly property bool connected: RemoteGamepad.clientConnected

    visible: RemoteGamepad.enabled
    soft: true
    iconPath: Theme.iconPhone
    iconColor: connected ? Theme.success : Theme.warning
    text: connected ? qsTr("GamePad: %1").arg(RemoteGamepad.clientName) : qsTr("Pair phone · %1").arg(RemoteGamepad.pin)
}
