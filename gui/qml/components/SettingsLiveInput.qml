import QtQuick
import QtQuick.Layouts
import gui 1.0

// Test mode readout: what WEMU actually receives from the controller, live.
SettingsCard {
    id: card

    title: "Live input"

    property var axes: ({})
    property var held: []
    property string lastButton: ""
    property int lastHeldMs: 0
    property real pressedAt: 0
    property real exitProgress: 0

    // Presses that began or ended while hidden are unknown, such as the B held to leave test mode.
    onVisibleChanged: {
        held = []
        lastButton = ""
    }

    function signedValue(value) { return (value < 0 ? "-" : " ") + Math.abs(value).toFixed(2) }

    function axis(name) { return axes[name] !== undefined ? axes[name] : InputManager.getAxis(name) }

    component StickPad: ColumnLayout {
        property string label: ""
        property real stickX: 0
        property real stickY: 0

        spacing: Theme.space2

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: parent.label
            font.pixelSize: Theme.fontCaption
            font.bold: true
            color: Theme.textMuted
        }

        Rectangle {
            id: pad
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 112
            implicitHeight: 112
            radius: width / 2
            color: Theme.surfaceMuted
            border.color: Theme.borderControl

            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: 1
                color: Theme.separator
            }

            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height
                color: Theme.separator
            }

            Rectangle {
                width: 16
                height: 16
                radius: 8
                color: Theme.accent
                x: (pad.width - width) / 2 + stickX * (pad.width - width) / 2
                y: (pad.height - height) / 2 + stickY * (pad.height - height) / 2
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "X " + card.signedValue(stickX) + "\nY " + card.signedValue(stickY)
            font.pixelSize: Theme.fontCaption
            font.family: "monospace"
            color: Theme.text
        }
    }

    component TriggerBar: RowLayout {
        property string label: ""
        property real amount: 0

        spacing: Theme.space3

        Text {
            Layout.preferredWidth: 24
            text: label
            font.pixelSize: Theme.fontCaption
            font.bold: true
            color: Theme.textMuted
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 8
            radius: 4
            color: Theme.surfaceMuted

            Rectangle {
                width: parent.width * amount
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }

        Text {
            Layout.preferredWidth: 44
            text: amount.toFixed(2)
            font.pixelSize: Theme.fontCaption
            font.family: "monospace"
            color: Theme.text
            horizontalAlignment: Text.AlignRight
        }
    }

    component Reading: RowLayout {
        property string label: ""
        property string value: ""

        Text {
            text: label
            font.pixelSize: Theme.fontCaption
            color: Theme.textMuted
        }

        Text {
            Layout.fillWidth: true
            text: value
            font.pixelSize: Theme.fontCaption
            font.bold: true
            color: Theme.text
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }

    Connections {
        target: InputManager
        enabled: card.visible

        function onAxisChanged(axisName, value, device) {
            const next = Object.assign({}, card.axes)
            next[axisName] = value
            card.axes = next
        }

        function onButtonChanged(button, pressed, device) {
            if (pressed) {
                card.pressedAt = Date.now()
                card.lastButton = button
                card.held = card.held.concat([button])
            } else {
                card.held = card.held.filter(name => name !== button)
                if (button === card.lastButton) card.lastHeldMs = Date.now() - card.pressedAt
            }
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.space6

        StickPad {
            label: "Left stick"
            stickX: card.axis("LX")
            stickY: card.axis("LY")
        }

        StickPad {
            label: "Right stick"
            stickX: card.axis("RX")
            stickY: card.axis("RY")
        }
    }

    TriggerBar {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        label: "LT"
        amount: card.axis("LT")
    }

    TriggerBar {
        Layout.fillWidth: true
        label: "RT"
        amount: card.axis("RT")
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        implicitHeight: 1
        color: Theme.separator
    }

    Reading {
        Layout.fillWidth: true
        label: "Buttons held"
        value: card.held.length > 0 ? card.held.join(" + ") : "None"
    }

    Reading {
        Layout.fillWidth: true
        label: "Last press"
        value: card.lastButton === "" ? "None"
             : card.held.indexOf(card.lastButton) >= 0 ? card.lastButton + " · holding"
             : card.lastButton + " · held " + card.lastHeldMs + " ms"
    }

    Item { Layout.fillHeight: true }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusSmall
        color: Theme.accentSoft
        clip: true

        Text {
            anchors.centerIn: parent
            text: "Hold B to exit test mode"
            font.pixelSize: Theme.fontBody
            font.bold: true
            color: Theme.accent
        }

        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: parent.width * card.exitProgress
            height: 4
            color: Theme.accent
        }
    }
}
