import QtQuick
import QtQuick.Controls
import gui 1.0

Button {
    id: control
    height: 52
    focusPolicy: Qt.NoFocus
    font.pixelSize: 17
    background: Rectangle {
        radius: Theme.sessionRadius
        color: control.highlighted || control.hovered ? Theme.sessionAccent : "#313b4c"
        opacity: control.enabled ? 1 : 0.5
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.sessionPanelText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
