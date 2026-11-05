import QtQuick
import QtQuick.Layouts
import gui 1.0

// One on/off setting: its label and description, with the switch beside them.
RowLayout {
    id: row

    property string label: ""
    property string description: ""
    property alias checked: toggle.checked
    property Item rightTarget: null
    property Item upTarget: null
    readonly property Item control: toggle
    signal controlFocused()
    signal toggled()

    spacing: Theme.space4

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1

        Text {
            Layout.fillWidth: true
            text: row.label
            font.pixelSize: Theme.fontBody
            font.bold: true
            color: Theme.text
        }

        Text {
            Layout.fillWidth: true
            text: row.description
            font.pixelSize: Theme.fontCaption
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
    }

    MenuSwitch {
        id: toggle
        KeyNavigation.right: row.rightTarget
        KeyNavigation.up: row.upTarget
        onActiveFocusChanged: if (activeFocus) row.controlFocused()
        Layout.preferredWidth: 150
        onToggled: row.toggled()
    }
}
