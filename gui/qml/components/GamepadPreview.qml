import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 430
    height: 420

    Column {
        anchors.centerIn: parent
        spacing: 10

        // ================= FRONT CONTROLLER =================

        Item {
            id: frontController
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
                source: "qrc:/assets/qml/assets/wiiuControllerBlank.png"
                scale: 0.96
                fillMode: Image.PreserveAspectCrop
                anchors.leftMargin: 15
                anchors.rightMargin: 5
            }

            // ================= FACE BUTTONS =================

            ControllerButton {
                x: 360
                y: 110
                width: 22
                height: 22
                label: "A"
                fontSize: 10
                pressed: aPressed
            }

            ControllerButton {
                x: 340
                y: 130
                width: 22
                height: 22
                label: "B"
                fontSize: 10
                pressed: bPressed
            }

            ControllerButton {
                x: 320
                y: 110
                width: 22
                height: 22
                label: "Y"
                fontSize: 10
                pressed: yPressed
            }

            ControllerButton {
                x: 340
                y: 90
                width: 22
                height: 22
                label: "X"
                fontSize: 10
                pressed: xPressed
            }

            // ================= LEFT STICK =================

            ControllerStick {
                id: leftStick
                x: 45
                y: 35
                width: 70
                height: 70
                pressed: leftStickPressed
                stickImage: "qrc:/assets/qml/assets/joystickXbox.png"

                Behavior on x { NumberAnimation { duration: 40 } }
                Behavior on y { NumberAnimation { duration: 40 } }
            }

            // ================= RIGHT STICK =================

            ControllerStick {
                id: rightStick
                x: 325
                y: 35
                width: 70
                height: 70
                pressed: rightStickPressed
                stickImage: "qrc:/assets/qml/assets/joystickXbox.png"

                Behavior on x { NumberAnimation { duration: 40 } }
                Behavior on y { NumberAnimation { duration: 40 } }
            }

            // ================= DPAD =================

            DpadButton {
                x: 85
                y: 126
                width: 10
                height: 11
                pressed: dpadDownPressed
            }

            DpadButton {
                x: 85
                y: 102
                width: 10
                height: 11
                pressed: dpadUpPressed
            }

            DpadButton {
                x: 72
                y: 114
                width: 11
                height: 10
                pressed: dpadLeftPressed
            }

            DpadButton {
                x: 96
                y: 114
                width: 11
                height: 10
                pressed: dpadRightPressed
            }
        }

        // ================= BACK CONTROLLER =================

        Image {
            id: backController
            width: 430
            fillMode: Image.PreserveAspectFit
            source: "qrc:/assets/qml/assets/wiiuGamepadBackview.png"
            scale: 0.85
        }
    }

    // ================= STATE =================

    property bool aPressed: false
    property bool bPressed: false
    property bool xPressed: false
    property bool yPressed: false

    property bool dpadDownPressed: false
    property bool dpadUpPressed: false
    property bool dpadRightPressed: false
    property bool dpadLeftPressed: false

    property bool leftStickPressed: false
    property bool rightStickPressed: false

    // ================= INPUT EVENTS =================

    Connections {
        target: InputManager

        function onButtonChanged(button, pressed, device) {

            if (button === "A")
                aPressed = pressed
            if (button === "B")
                bPressed = pressed
            if (button === "X")
                xPressed = pressed
            if (button === "Y")
                yPressed = pressed

            if (button === "DPAD_DOWN")
                dpadDownPressed = pressed
            if (button === "DPAD_UP")
                dpadUpPressed = pressed
            if (button === "DPAD_LEFT")
                dpadLeftPressed = pressed
            if (button === "DPAD_RIGHT")
                dpadRightPressed = pressed

            if (button === "LEFTSTICK_BUTTON")
                leftStickPressed = pressed
            if (button === "RIGHTSTICK_BUTTON")
                rightStickPressed = pressed
        }

        function onAxisChanged(axis, value, device) {

            if (axis === "LX")
                leftStick.x = 45 + value * 16

            if (axis === "LY")
                leftStick.y = 35 + value * 16

            if (axis === "RX")
                rightStick.x = 325 + value * 16

            if (axis === "RY")
                rightStick.y = 35 + value * 16
        }
    }
}
