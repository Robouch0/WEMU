import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Button {
    id: control
    height: 52
    focusPolicy: Qt.NoFocus
    font.pixelSize: 17
    background: Rectangle {
        radius: Theme.radius
        color: control.highlighted || control.hovered ? Theme.accent : "#313b4c"
        opacity: control.enabled ? 1 : 0.5
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.panelText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
