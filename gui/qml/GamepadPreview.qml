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
        source: "qrc:/assets/qml/assets/wiiuControllerBlank.png"
        scale : 0.96
        fillMode: Image.PreserveAspectCrop
        anchors.leftMargin: 15
        anchors.rightMargin: 5
    }

    // ================= BUTTON A =================
    Rectangle {
        id: aButton
        x: 360; y: 110
        width: 22; height: 22
        radius: 11
        border.color: aPressed ? "#3498ff" : "white"
        border.width: 2
        color: aPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "A"
            font.family: "Helvetica"
            font.pointSize: 10
            color: aPressed ? "#3498ff" : "white"
        }
    }

    // ================= BUTTON B =================
    Rectangle {
        id: bButton
        x: 340; y: 130
        width: 22; height: 22
        radius: 11
        border.color: bPressed ? "#3498ff" : "white"
        border.width: 2
        color: bPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "B"
            font.family: "Helvetica"
            font.pointSize: 10
            color: bPressed ? "#3498ff" : "white"
        }
    }

    // ================= BUTTON Y =================
    Rectangle {
        id: yButton
        x: 320; y: 110
        width: 22; height: 22
        radius: 11
        border.color: yPressed ? "#3498ff" : "white"
        border.width: 2
        color: yPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "Y"
            font.family: "Helvetica"
            font.pointSize: 10
            color: yPressed ? "#3498ff" : "white"
        }
    }

    // ================= BUTTON X =================
    Rectangle {
        id: xButton
        x: 340; y: 90
        width: 22; height: 22
        radius: 11
        border.color: xPressed ? "#3498ff" : "white"
        border.width: 2
        color: xPressed ? "transparent" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "X"
            font.family: "Helvetica"
            font.pointSize: 10
            color: xPressed ? "#3498ff" : "white"
        }
    }

    // ================= LEFT STICK =================
    Rectangle {
        id: leftStick
        width: 70; height: 70
        radius: 30
        clip: true
        color: "transparent"
        x:45
        y:35

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
        x:325
        y:35

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
    property bool xPressed: false
    property bool yPressed: false

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
        }

        function onAxisChanged(axis, value, device) {
            if (axis === "LX")
                leftStick.x = 45 + value * 17
            if (axis === "LY")
                leftStick.y = 35 + value * 17

            if (axis === "RX")
                rightStick.x = 325 + value * 17
            if (axis === "RY")
                rightStick.y = 35 + value * 17
        }
    }
}
