import QtQuick
import QtQuick.Controls.Basic as Basic
import gui 1.0

Basic.Slider {
    id: control

    implicitHeight: Theme.controlHeight

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 240
        width: control.availableWidth
        height: 6
        radius: 3
        color: Theme.borderControl

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 24
        implicitHeight: 24
        radius: 12
        color: Theme.surface
        border.color: Theme.accent
        border.width: 2

        FocusRing {
            shown: control.activeFocus
            cornerRadius: 12
        }
    }
}
