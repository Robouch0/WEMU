import QtQuick
import QtQuick.Controls

Column {
    spacing: 10
    Text {
        text: "Devices: " + InputManager.connectedDevices().join(", ")
    }

    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            if (InputManager.isButtonPressed("A"))
                keyLabel.text = "A Pressed"
            else
                keyLabel.text = "A Released"
        }
    }

    Text {
        id: keyLabel
        text: "Waiting for input..."
        font.bold: true
        color: "orange"
    }
}
