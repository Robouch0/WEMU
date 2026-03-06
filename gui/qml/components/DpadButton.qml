import QtQuick

Rectangle {
    property bool pressed: false

    width: 12
    height: 17
    radius: 1
    border.width: 1
    border.color: pressed ? "#3498ff" : "white"
    color: pressed ? "#3498ff" : "white"
}
