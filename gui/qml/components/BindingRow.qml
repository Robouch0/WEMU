import QtQuick
import QtQuick.Layouts
import gui 1.0

Rectangle {
    id: root

    property string wiiuLabel: ""
    property string xboxButton: ""
    property bool isListening: false
    property bool focused: false

    signal rowClicked()

    height: 30
    radius: Theme.radiusSmall
    color: root.isListening ? Theme.accentSoft
                            : (mouseArea.containsMouse ? Theme.surfaceMuted : "transparent")
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    FocusRing {
        anchors.margins: 0
        shown: root.focused
        cornerRadius: 0
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.rowClicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space3
        anchors.rightMargin: Theme.space3
        spacing: 0

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: root.isListening ? "Press a button…" : (root.xboxButton !== "" ? root.xboxButton : "—")
            color: root.isListening ? Theme.accent : Theme.text
            font.pixelSize: Theme.fontBody
            font.bold: root.isListening
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: root.wiiuLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }
    }
}
