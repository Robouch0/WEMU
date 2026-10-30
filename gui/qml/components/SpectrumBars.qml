import QtQuick
import gui 1.0

// The menu audio spectrum. Simple rectangles keep it light while it animates.
Item {
    id: graph

    Row {
        anchors.fill: parent
        spacing: Theme.space1

        Repeater {
            model: MenuAudio.spectrum.length

            Item {
                required property int index
                width: (graph.width - (MenuAudio.spectrum.length - 1) * Theme.space1) / MenuAudio.spectrum.length
                height: graph.height

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.max(2, parent.height * MenuAudio.spectrum[index])
                    radius: 2
                    color: Theme.accent
                    opacity: Math.min(1, 0.3 + 3 * MenuAudio.spectrum[index])
                }
            }
        }
    }
}
