import QtQuick
import QtQuick.Layouts
import gui 1.0

Rectangle {
    property string title: ""
    property bool highlighted: false
    default property alias content: body.data

    implicitHeight: layout.implicitHeight + 2 * Theme.space4
    radius: Theme.radiusMedium
    color: Theme.surface
    border.color: highlighted ? Theme.accent : Theme.separator
    border.width: highlighted ? 2 : 1

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.space4
        spacing: Theme.space3

        Text {
            visible: text !== ""
            Layout.fillWidth: true
            text: title
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            color: Theme.text
            elide: Text.ElideRight
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space3
        }
    }
}
