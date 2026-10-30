import QtQuick
import gui 1.0

Rectangle {
    id: root

    property string label: ""
    property bool pressed: false
    property int fontSize: 15

    width: 25
    height: 25
    radius: 12

    border.width: 2
    border.color: pressed ? Theme.highlightOnArt : "white"
    color: "transparent"

    Text {
        anchors.centerIn: parent
        text: root.label
        font.family: "sans-serif"
        font.pointSize: root.fontSize
        color: root.pressed ? Theme.highlightOnArt : "white"
    }
}
