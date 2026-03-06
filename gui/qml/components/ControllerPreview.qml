import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 435
    height: 480

    Column {
        anchors.centerIn: parent
        spacing: 5

        // ================= FRONT CONTROLLER =================

        Item {
            id: frontController
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
                source: "qrc:/assets/qml/assets/xboxControllerBlank.png"
                fillMode: Image.PreserveAspectCrop
                scale: 0.85
            }

            // ================= FACE BUTTONS =================

            ControllerButton {
                x: 290
                y: 105
                label: "A"
                pressed: aPressed
            }

            ControllerButton {
                x: 315
                y: 80
                label: "B"
                pressed: bPressed
            }

            ControllerButton {
                x: 265
                y: 80
                label: "X"
                pressed: xPressed
            }

            ControllerButton {
                x: 290
                y: 60
                label: "Y"
                pressed: yPressed
            }

            // ================= LEFT STICK =================

            ControllerStick {
                id: leftStick
                x: 98
                y: 62
                pressed: leftStickPressed
                stickImage: "qrc:/assets/qml/assets/joystickXbox.png"

                Behavior on x { NumberAnimation { duration: 40 } }
                Behavior on y { NumberAnimation { duration: 40 } }
            }

            // ================= RIGHT STICK =================

            ControllerStick {
                id: rightStick
                x: 223
                y: 111
                pressed: rightStickPressed
                stickImage: "qrc:/assets/qml/assets/joystickXbox.png"

                Behavior on x { NumberAnimation { duration: 40 } }
                Behavior on y { NumberAnimation { duration: 40 } }
            }

            // ================= DPAD =================

            DpadButton {
                x: 166
                y: 151
                pressed: dpadDownPressed
            }

            DpadButton {
                x: 166
                y: 122
                pressed: dpadUpPressed
            }

            DpadButton {
                x: 149
                y: 139
                width: 17
                height: 12
                pressed: dpadLeftPressed
            }

            DpadButton {
                x: 178
                y: 139
                width: 17
                height: 12
                pressed: dpadRightPressed
            }
        }

        // ================= BACK CONTROLLER =================

        Image {
            id: backController
            width: 435
            fillMode: Image.PreserveAspectFit
            source: "qrc:/assets/qml/assets/xboxControllerBackview.png"
            scale: 0.9
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
                leftStick.x = 98 + value * 18

            if (axis === "LY")
                leftStick.y = 62 + value * 18

            if (axis === "RX")
                rightStick.x = 223 + value * 18

            if (axis === "RY")
                rightStick.y = 111 + value * 18
        }
    }
}
