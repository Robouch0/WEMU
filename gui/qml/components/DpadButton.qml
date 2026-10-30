import QtQuick
import gui 1.0

Rectangle {
    id: root

    property bool pressed: false

    width: 12
    height: 17
    radius: 1
    border.width: 1
    border.color: pressed ? Theme.highlightOnArt : "white"
    color: pressed ? Theme.highlightOnArt : "white"
}
