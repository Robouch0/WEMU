import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 435
    height: 480

    Column {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10

        // ================= FRONT CONTROLLER =================
        Item {
            id: frontController
            width: 435
            height: 285

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

            // ================= MENU BUTTONS =================
            ControllerButton {
                x: 230
                y: 85
                label: ""
                width: 15
                height: 15
                pressed: startPressed
            }

            ControllerButton {
                x: 184
                y: 85
                label: ""
                width: 15
                height: 15
                pressed: backPressed
            }

            Rectangle {
                id: guideButton

                width: 29
                height: 29
                radius: 25
                x: 200
                y: 47

                border.width: 2
                border.color: guidePressed ? "green" : "transparent"
                color: "transparent"
            }

            // ================= LEFT STICK =================
            ControllerStick {
                id: leftStick
                x: 98
                y: 62
                pressed: leftStickPressed
                stickImage: "qrc:/assets/qml/assets/joystickXbox.png"

                Behavior on x {
                    NumberAnimation {
                        duration: 40
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: 40
                    }
                }
            }

            // ================= RIGHT STICK =================
            ControllerStick {
                id: rightStick
                x: 223
                y: 111
                pressed: rightStickPressed
                stickImage: "qrc:/assets/qml/assets/joystickXbox.png"

                Behavior on x {
                    NumberAnimation {
                        duration: 40
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: 40
                    }
                }
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
        Item {
            id: backController
            width: 435
            height: 290

            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                source: "qrc:/assets/qml/assets/xboxControllerBackview.png"
                scale: 0.95
            }

            Rectangle {
                id: buttonLT
                x: 120
                y: 115
                width: 40
                height: 25
                radius: 10

                border.width: 2
                border.color: ltPressed ? "#3498ff" : "white"
                color: "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "LT"
                    font.family: "sans-serif"
                    font.pointSize: 14
                    color: ltPressed ? "#3498ff" : "white"
                }
            }

            Rectangle {
                id: buttonRT
                x: 275
                y: 115
                width: 40
                height: 25
                radius: 10

                border.width: 2
                border.color: rtPressed ? "#3498ff" : "white"
                color: "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "RT"
                    font.family: "sans-serif"
                    font.pointSize: 12
                    color: rtPressed ? "#3498ff" : "white"
                }
            }

            Rectangle {
                id: buttonRB
                x: 274
                y: 85
                width: 40
                height: 18
                radius: 11

                border.width: 2
                border.color: rbPressed ? "#3498ff" : "white"
                color: "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "RB"
                    font.family: "sans-serif"
                    font.pointSize: 11
                    color: rbPressed ? "#3498ff" : "white"
                }
            }

            Rectangle {
                id: buttonLB
                x: 120
                y: 85
                width: 40
                height: 18
                radius: 11

                border.width: 2
                border.color: lbPressed ? "#3498ff" : "white"
                color: "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "LB"
                    font.family: "sans-serif"
                    font.pointSize: 11
                    color: lbPressed ? "#3498ff" : "white"
                }
            }
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

    property bool ltPressed: false
    property bool rtPressed: false
    property bool lbPressed: false
    property bool rbPressed: false

    property bool startPressed: false
    property bool backPressed: false
    property bool guidePressed: false


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

            if (button === "LT")
                ltPressed = pressed
            if (button === "RT")
                rtPressed = pressed
            if (button === "LB")
                lbPressed = pressed
            if (button === "RB")
                rbPressed = pressed

            if (button === "START")
                startPressed = pressed
            if (button === "BACK")
                backPressed = pressed
            if (button === "XBOX_GUIDE")
                guidePressed = pressed
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
