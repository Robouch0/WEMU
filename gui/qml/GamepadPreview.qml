import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 430
    height: 270


    MouseArea {
        anchors.fill: parent

        onClicked: (mouse) => {
            console.log("Clicked at:", mouse.x, mouse.y)
        }
    }

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
    Rectangle {
        id: leftStick
        width: 70; height: 70
        radius: 30
        clip: true
        color: "transparent"
        x:60
        y:61

        Image {
            anchors.fill: parent
            width: 68; height: 68
            source: "qrc:/assets/qml/assets/joystickXbox.png"
            fillMode: Image.PreserveAspectCrop
        }
        Behavior on x { NumberAnimation { duration: 40 } }
        Behavior on y { NumberAnimation { duration: 40 } }
    }

    // ================= RIGHT STICK =================
    Rectangle {
        id: rightStick
        width: 70; height: 70
        radius: 30
        clip: true
        color: "transparent"
        x:320
        y:67

        Image {
            anchors.fill: parent
            width: 68; height: 68
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
                leftStick.x = 60 + value * 18
            if (axis === "LY")
                leftStick.y = 61 + value * 18

            if (axis === "RX")
                rightStick.x = 320 + value * 18
            if (axis === "RY")
                rightStick.y = 67 + value * 18
        }
    }
}
