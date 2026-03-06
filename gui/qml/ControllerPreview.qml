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
        source: "qrc:/assets/qml/assets/xboxControllerBlank.png"
        fillMode: Image.PreserveAspectCrop
        scale: 0.85
    }

    // ================= BUTTON A =================
    Rectangle {
        id: aButton
        x: 290; y: 105
        width: 25; height: 25
        radius: 12
        border.color: aPressed ? "#3498ff" : "white"
        border.width: 2
        color: aPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "A"
            font.family: "Helvetica"
            font.pointSize: 15
            color: aPressed ? "#3498ff" : "white"
        }
    }

    // ================= BUTTON B =================
    Rectangle {
        id: bButton
        x: 315; y: 80
        width: 25; height: 25
        radius: 12
        border.color: bPressed ? "#3498ff" : "white"
        border.width: 2
        color: bPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "B"
            font.family: "Helvetica"
            font.pointSize: 15
            color: bPressed ? "#3498ff" : "white"
        }
    }

    // ================= BUTTON X =================
    Rectangle {
        id: xButton
        x: 265; y: 80
        width: 25; height: 25
        radius: 12
        border.color: xPressed ? "#3498ff" : "white"
        border.width: 2
        color: xPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "X"
            font.family: "Helvetica"
            font.pointSize: 15
            color: xPressed ? "#3498ff" : "white"
        }
    }

    // ================= BUTTON Y =================
    Rectangle {
        id: yButton
        x: 290; y: 60
        width: 25; height: 25
        radius: 12
        border.color: yPressed ? "#3498ff" : "white"
        border.width: 2
        color: yPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "Y"
            font.family: "Helvetica"
            font.pointSize: 15
            color: yPressed ? "#3498ff" : "white"
        }
    }

    // ================= LEFT STICK =================
    Rectangle {
        id: leftStick
        width: 80; height: 80
        radius: 50
        clip: true
        color: "transparent"
        x:98
        y:62

        Image {
            anchors.fill: parent
            width: 75; height: 75
            source: "qrc:/assets/qml/assets/joystickXbox.png"
            fillMode: Image.PreserveAspectCrop
        }
        Rectangle {
            width: 30
            height: 30
            radius: 25
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: -4.7
            anchors.verticalCenterOffset: -6.9

            color: leftStickPressed ? "#3498ff" : "transparent"
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
        x:223
        y:111

        Image {
            anchors.fill: parent
            width: 75; height: 75
            source: "qrc:/assets/qml/assets/joystickXbox.png"
            fillMode: Image.PreserveAspectCrop
        }
        Rectangle {
            width: 30
            height: 30
            radius: 25
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: -4.7
            anchors.verticalCenterOffset: -6.9

            color: rightStickPressed ? "#3498ff" : "transparent"
        }
        Behavior on x { NumberAnimation { duration: 40 } }
        Behavior on y { NumberAnimation { duration: 40 } }
    }

    // ================= DPAD DOWN =================
    Rectangle {
        id: dpadDown
        width: 12; height: 17
        border.color: dpadDownPressed ? "#3498ff" : "white"
        border.width: 1
        radius: 1
        color: dpadDownPressed ? "#3498ff" : "white"
        x:166
        y:151
    }

    // ================= DPAD UP =================
    Rectangle {
        id: dpadUp
        width: 12; height: 17
        border.color: dpadUpPressed ? "#3498ff" : "white"
        border.width: 1
        radius: 1
        color: dpadUpPressed ? "#3498ff" : "white"
        x:166
        y:122
    }

    // ================= DPAD LEFT =================
    Rectangle {
        id: dpadLeft
        width: 17; height: 12
        border.color: dpadLeftPressed ? "#3498ff" : "white"
        border.width: 1
        radius: 1
        color: dpadLeftPressed ? "#3498ff" : "white"
        x:149
        y:139
    }

    // ================= DPAD RIGHT =================
    Rectangle {
        id: dpadRight
        width: 17; height: 12
        border.color: dpadRightPressed ? "#3498ff" : "white"
        border.width: 1
        radius: 1
        color: dpadRightPressed ? "#3498ff" : "white"
        x:178
        y:139
    }


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
