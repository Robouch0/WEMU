import QtQuick
import QtQuick.Controls.Basic as Basic
import gui 1.0

// One color choice. The check mark and the name beside the row keep it clear without relying on color.
Basic.AbstractButton {
    id: swatch

    property string colorName: ""
    readonly property bool chosen: AppPreferences.accentColor === colorName

    implicitWidth: 44
    implicitHeight: 44

    onClicked: {
        AppPreferences.accentColor = colorName
        MenuAudio.play("confirm")
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Item {
        Rectangle {
            id: dot
            anchors.centerIn: parent
            width: 36
            height: 36
            radius: 18
            color: Theme.accentFor(swatch.colorName)
            scale: swatch.down ? 0.92 : swatch.hovered ? 1.08 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

            MenuIcon {
                anchors.centerIn: parent
                visible: swatch.chosen
                path: Theme.iconCheck
                color: Theme.onAccentFor(swatch.colorName)
            }

            FocusRing {
                shown: swatch.activeFocus
                cornerRadius: 18
            }
        }
    }
}
