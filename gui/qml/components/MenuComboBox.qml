import QtQuick
import QtQuick.Controls.Basic as Basic
import gui 1.0

Basic.ComboBox {
    id: control

    // A closed ComboBox would otherwise use Up and Down to change its value.
    property Item navigateUp: null
    property Item navigateDown: null

    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontBody
    leftPadding: Theme.space3

    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.text: Theme.text
    palette.window: Theme.surface
    palette.base: Theme.surface
    palette.light: Theme.surfaceMuted
    palette.mid: Theme.surfaceHover
    palette.dark: Theme.borderControl
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.textOnAccent

    popup.enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationFast; easing.type: Easing.OutCubic }
        }
    }

    Keys.onUpPressed: event => {
        if (popup.visible) event.accepted = false
        else if (navigateUp) navigateUp.forceActiveFocus()
    }
    Keys.onDownPressed: event => {
        if (popup.visible) event.accepted = false
        else if (navigateDown) navigateDown.forceActiveFocus()
    }

    delegate: Basic.ItemDelegate {
        id: option
        required property int index
        required property var modelData

        width: control.popup.availableWidth
        highlighted: control.highlightedIndex === index
        font: control.font

        contentItem: Text {
            text: control.textRole !== "" ? option.modelData[control.textRole] : option.modelData
            font: option.font
            color: option.highlighted ? Theme.textOnAccent : Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: option.highlighted ? Theme.accent : Theme.surface
        }
    }

    background: Rectangle {
        implicitWidth: 240
        radius: Theme.radiusSmall
        color: control.hovered ? Theme.surfaceMuted : Theme.surface
        border.color: Theme.borderControl

        FocusRing { shown: control.activeFocus && !control.popup.visible }
    }
}
