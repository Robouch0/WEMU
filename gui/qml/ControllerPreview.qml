import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 500
    height: 300

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
    Rectangle {
        id: leftStick
        width: 40; height: 40
        radius: 20
        border.color: "#3498ff"
        color: "#3498ff55"

        x: 120
        y: 150

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
                leftStick.x = 120 + value * 20

            if (axis === "LY")
                leftStick.y = 150 + value * 20
        }
    }
}
