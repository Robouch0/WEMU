import QtQuick
import QtQuick.Layouts
import gui 1.0

// One setting: a label, a line of description, and its control below them.
ColumnLayout {
    property string label: ""
    property string description: ""

    spacing: Theme.space1

    Text {
        Layout.fillWidth: true
        text: label
        font.pixelSize: Theme.fontBody
        font.bold: true
        color: Theme.text
    }

    Text {
        visible: text !== ""
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.space1
        text: description
        font.pixelSize: Theme.fontCaption
        color: Theme.textMuted
        wrapMode: Text.WordWrap
    }
}
