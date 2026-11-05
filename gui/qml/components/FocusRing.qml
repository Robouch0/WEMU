import QtQuick
import gui 1.0

// The shared focus indicator: an accent outline just outside its parent.
Rectangle {
    property bool shown: false
    property int cornerRadius: Theme.radiusSmall

    anchors.fill: parent
    anchors.margins: -(Theme.focusRingOffset + Theme.focusRingWidth)
    radius: cornerRadius + Theme.focusRingOffset + Theme.focusRingWidth
    color: "transparent"
    border.color: Theme.accent
    border.width: Theme.focusRingWidth
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
}
