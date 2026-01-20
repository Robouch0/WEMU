import QtQuick
import QtQuick.Controls
import gui 1.0

Column {
    spacing: 10
    Text {
        text: "Devices: " + InputManager.connectedDevices().join(", ")
    }

    Row {
        spacing: 40

        Column {
            Text { text: "Physical Controller"; font.pixelSize: 22 }
            ControllerPreview {}
        }
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
