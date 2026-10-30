import QtQuick
import QtQuick.Controls.Basic as Basic
import gui 1.0

Basic.Button {
    id: control

    property bool primary: false
    property bool destructive: false
    property int textLeftInset: 0

    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space4 + textLeftInset
    rightPadding: Theme.space4
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: Theme.fontBody
    font.bold: primary
    opacity: enabled ? 1 : 0.5

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.textOnAccent : control.destructive ? Theme.danger : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 96
        radius: Theme.radiusSmall
        color: control.primary ? (control.hovered || control.down ? Theme.accentHover : Theme.accent)
                               : (control.hovered || control.down ? Theme.surfaceHover : Theme.surfaceMuted)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusRing { shown: control.activeFocus }
    }
}
