import QtQuick
import QtQuick.Controls.Basic as Basic
import gui 1.0

Basic.Button {
    id: control

    property bool primary: false
    property bool soft: false
    property bool destructive: false
    property string iconPath: ""
    readonly property color contentColor: primary ? Theme.textOnAccent
                                        : soft ? (hovered ? Theme.accentHover : Theme.accent)
                                        : destructive ? Theme.danger : Theme.text
    property color iconColor: contentColor

    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space4
    rightPadding: Theme.space4
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: Theme.fontBody
    font.bold: primary
    opacity: enabled ? 1 : 0.5
    scale: down ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Item {
        id: content
        readonly property real iconSpace: control.iconPath !== "" ? 28 : 0
        implicitWidth: label.implicitWidth + iconSpace
        implicitHeight: label.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.space2

            MenuIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconPath !== ""
                path: control.iconPath
                color: control.iconColor
            }

            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, content.width - content.iconSpace)
                text: control.text
                font: control.font
                color: control.contentColor
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        implicitWidth: 96
        radius: Theme.radiusSmall
        color: control.primary ? (control.hovered || control.down ? Theme.accentHover : Theme.accent)
             : control.soft ? (control.hovered || control.down ? Theme.accentSoftHover : Theme.accentSoft)
             : (control.hovered || control.down ? Theme.surfaceHover : Theme.surfaceMuted)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusRing { shown: control.activeFocus }
    }
}
