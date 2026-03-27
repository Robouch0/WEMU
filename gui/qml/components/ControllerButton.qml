import QtQuick

Rectangle {
    id: root

    property string label: ""
    property bool pressed: false
    property int fontSize: 15

    width: 25
    height: 25
    radius: 12

    border.width: 2
    border.color: pressed ? "#3498ff" : "white"
    color: "transparent"

    Text {
        anchors.centerIn: parent
        text: root.label
        font.family: "sans-serif"
        font.pointSize: root.fontSize
        color: root.pressed ? "#3498ff" : "white"
    }
}
