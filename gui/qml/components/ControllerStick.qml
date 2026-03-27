import QtQuick

Rectangle {
    id: root

    property string stickImage: ""
    property bool pressed: false

    width: 80
    height: 80
    radius: 50
    clip: true
    color: "transparent"

    Image {
        anchors.fill: parent
        source: root.stickImage
        fillMode: Image.PreserveAspectCrop
    }

    Rectangle {
        width: 30
        height: 30
        radius: 25
        anchors.centerIn: parent
        // offsets compensate for the visual center of the joystickXbox.png asset
        anchors.horizontalCenterOffset: -4.7
        anchors.verticalCenterOffset: -6.9

        color: root.pressed ? "#3498ff" : "transparent"
    }
}
