import QtQuick
import QtQuick.Controls.Basic as Basic
import gui 1.0

Basic.Switch {
    id: control

    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontBody
    text: checked ? "On" : "Off"
    spacing: Theme.space2

    Keys.onReturnPressed: toggle()
    Keys.onEnterPressed: toggle()

    indicator: Rectangle {
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        implicitWidth: 52
        implicitHeight: 28
        radius: 14
        color: control.checked ? Theme.accent : Theme.surfaceMuted
        border.color: control.checked ? Theme.accent : Theme.borderControl
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 4 : 4
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            radius: 10
            color: control.checked ? Theme.textOnAccent : Theme.textMuted
            Behavior on x { NumberAnimation { duration: Theme.durationFast } }
        }

        FocusRing {
            shown: control.activeFocus
            cornerRadius: 14
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
    }
}
