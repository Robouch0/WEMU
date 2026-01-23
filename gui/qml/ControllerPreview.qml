import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 435
    height: 285

    MouseArea {
        anchors.fill: parent

        onClicked: (mouse) => {
            console.log("Clicked at:", mouse.x, mouse.y)
        }
    }


    Image {
        anchors.fill: parent
        source: "qrc:/assets/qml/assets/xboxController.png"
        fillMode: Image.PreserveAspectCrop
    }

    // ================= BUTTON A =================
    Rectangle {
        id: aButton
        x: 293; y: 120
        width: 27; height: 27
        radius: 12
        border.color: "#3498ff"
        border.width: 3
        color: aPressed ? "#3498ff" : "transparent"
    }

    // ================= BUTTON B =================
    Rectangle {
        id: bButton
        x: 315; y: 99
        width: 27; height: 27
        radius: 13
        border.color: "#3498ff"
        border.width: 3
        color: bPressed ? "#3498ff" : "transparent"
    }

    // ================= LEFT STICK =================
    Rectangle {
        id: leftStick
        width: 80; height: 80
        radius: 50
        clip: true
        color: "transparent"
        x:107
        y:73

        Image {
            anchors.fill: parent
            width: 75; height: 75
            source: "qrc:/assets/qml/assets/joystickXbox.png"
            fillMode: Image.PreserveAspectCrop
        }
        Behavior on x { NumberAnimation { duration: 40 } }
        Behavior on y { NumberAnimation { duration: 40 } }
    }

    // ================= RIGHT STICK =================
    Rectangle {
        id: rightStick
        width: 80; height: 80
        radius: 50
        clip: true
        color: "transparent"
        x:231
        y:124

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
                leftStick.x = 107 + value * 18
            if (axis === "LY")
                leftStick.y = 73 + value * 18

            if (axis === "RX")
                rightStick.x = 231 + value * 18
            if (axis === "RY")
                rightStick.y = 124 + value * 18
        }
    }
}
