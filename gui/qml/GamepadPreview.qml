import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 520
    height: 330

    Image {
        anchors.fill: parent
        source: "qrc:/assets/qml/assets/wiiuGamepad.png"
        fillMode: Image.PreserveAspectCrop
    }

    // ================= BUTTON A =================
    Rectangle {
        id: aButton
        x: 340; y: 140
        width: 30; height: 30
        radius: 15
        border.color: "#3498ff"
        border.width: 2
        color: aPressed ? "#3498ff" : "transparent"
    }

    // ================= BUTTON B =================
    Rectangle {
        id: bButton
        x: 380; y: 110
        width: 30; height: 30
        radius: 15
        border.color: "#3498ff"
        border.width: 2
        color: bPressed ? "#3498ff" : "transparent"
    }

    // ================= LEFT STICK =================
    // Rectangle {
    //     id: leftStick
    //     width: 40; height: 40
    //     radius: 20
    //     border.color: "#3498ff"
    //     color: "#3498ff55"
    //
    //     x: 120
    //     y: 150
    //
    //     Behavior on x { NumberAnimation { duration: 40 } }
    //     Behavior on y { NumberAnimation { duration: 40 } }
    // }
    Rectangle {
        id: leftStick
        width: 80; height: 80
        radius: 50
        clip: true
        color: "transparent"
        x:160
        y:70

        Image {
            anchors.fill: parent
            width: 75; height: 75
            source: "qrc:/assets/qml/assets/joystickXbox.png"
            fillMode: Image.PreserveAspectCrop
        }
        Behavior on x { NumberAnimation { duration: 40 } }
        Behavior on y { NumberAnimation { duration: 40 } }
    }



    property bool aPressed: false
    property bool bPressed: false

    Connections {
        target: InputManager

        function onButtonChanged(button, pressed, device) {
            if (button === "A")
                aPressed = pressed

            if (button === "B")
                bPressed = pressed
        }

        function onAxisChanged(axis, value, device) {
            if (axis === "LX")
                leftStick.x = 160 + value * 20

            if (axis === "LY")
                leftStick.y = 70 + value * 20
        }
    }
}
