import QtQuick
import QtQuick.Controls

// Header pill: mobile GamePad status at a glance; opens the pairing panel.
Rectangle {
    id: badge
    width: label.implicitWidth + 40
    height: 36
    radius: 18
    color: mouse.containsMouse ? "#d0d0d5" : "#e4e4ea"
    visible: RemoteGamepad.enabled
    Behavior on color { ColorAnimation { duration: 100 } }

    Rectangle {
        id: dot
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 9
        height: 9
        radius: 4.5
        color: RemoteGamepad.clientConnected ? "#1e9e5a" : "#f0a020"
        SequentialAnimation on opacity {
            running: !RemoteGamepad.clientConnected
            loops: Animation.Infinite
            NumberAnimation { to: 0.35; duration: 800; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
        }
    }

    Text {
        id: label
        anchors.left: dot.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: 13
        font.bold: true
        color: "#1a1a1a"
        text: RemoteGamepad.clientConnected ? "GamePad: " + RemoteGamepad.clientName : "Pair phone · " + RemoteGamepad.pin
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.open()
    }

    Popup {
        id: popup
        y: badge.height + 8
        x: badge.width - width
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Item {}
        RemoteGamepadPanel { width: 420 }
    }
}
