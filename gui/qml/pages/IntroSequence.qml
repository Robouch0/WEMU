import QtQuick
import QtMultimedia
import gui 1.0

// The mascot clip WEMU opens with, over the library. It ends on the WEMU logo at the top of the
// screen: that logo flies into the library header while the clip fades into the library.
// Any key, button or click skips it.
Item {
    id: intro
    anchors.fill: parent
    focus: true

    // Where the clip's last frame shows the logo, in the clip's 1280x720 pixels.
    readonly property rect clipLogo: Qt.rect(521, 38, 239, 52.4)
    readonly property size clipSize: Qt.size(1280, 720)
    property bool leaving: false

    // The clip fills the window and is cropped to it: the same mapping VideoOutput applies.
    function fromClip(area) {
        const scale = Math.max(width / clipSize.width, height / clipSize.height)
        return Qt.rect((width - clipSize.width * scale) / 2 + area.x * scale, (height - clipSize.height * scale) / 2 + area.y * scale,
                       area.width * scale, area.height * scale)
    }

    // The clip ended: its logo becomes the header's.
    function handOver() {
        if (leaving) return
        leaving = true
        player.pause()
        const header = mainLoader.item ? mainLoader.item.headerLogo : null
        if (!header) {
            skip()
            return
        }
        const start = fromClip(clipLogo)
        const end = header.mapToItem(intro, 0, 0)
        // The clip's own logo, glow included, would stay behind as the copy flies off.
        const margin = start.height * 0.4
        logoCover.x = start.x - margin
        logoCover.y = start.y - margin
        logoCover.width = start.width + 2 * margin
        logoCover.height = start.height + 2 * margin
        logoCover.visible = true
        flyer.x = start.x
        flyer.y = start.y
        flyer.height = start.height
        flyer.visible = true
        flyX.to = end.x
        flyY.to = end.y
        flyHeight.to = header.height
        handOverAnimation.start()
    }

    // Straight to the library, without the logo's flight.
    function skip() {
        leaving = true
        rootWindow.introPlaying = false
        fadeOut.start()
    }

    Component.onCompleted: forceActiveFocus()

    MediaPlayer {
        id: player
        source: "qrc:/assets/qml/assets/intro.mp4"
        videoOutput: video
        autoPlay: true
        // Stop on the last frame rather than after it, so the logo is still on screen.
        onPositionChanged: if (player.duration > 0 && player.position >= player.duration - 120) intro.handOver()
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia) intro.handOver()
        onErrorOccurred: (error, errorString) => {
            console.warn("Intro clip:", errorString)
            intro.skip()
        }
    }

    Rectangle {
        id: stage
        anchors.fill: parent
        color: "#06070c" // the clip's own background, for letterboxed first frames

        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectCrop
        }

        Rectangle {
            id: logoCover
            visible: false
            color: stage.color
            radius: height / 2
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space5
            visible: !intro.leaving
            text: qsTr("Press any button to skip")
            font.pixelSize: Theme.fontCaption
            color: "#c8d0dc"
            opacity: 0
            SequentialAnimation on opacity {
                PauseAnimation { duration: 1500 }
                NumberAnimation { to: 0.85; duration: 600 }
            }
        }
    }

    WemuLogo {
        id: flyer
        visible: false
    }

    SequentialAnimation {
        id: handOverAnimation
        ParallelAnimation {
            NumberAnimation { target: stage; property: "opacity"; to: 0; duration: 900; easing.type: Easing.InOutQuad }
            NumberAnimation { id: flyX; target: flyer; property: "x"; duration: 900; easing.type: Easing.InOutCubic }
            NumberAnimation { id: flyY; target: flyer; property: "y"; duration: 900; easing.type: Easing.InOutCubic }
            NumberAnimation { id: flyHeight; target: flyer; property: "height"; duration: 900; easing.type: Easing.InOutCubic }
        }
        ScriptAction {
            script: {
                rootWindow.introPlaying = false
                rootWindow.introVisible = false
            }
        }
    }

    SequentialAnimation {
        id: fadeOut
        NumberAnimation { target: intro; property: "opacity"; to: 0; duration: 400; easing.type: Easing.InOutQuad }
        ScriptAction { script: rootWindow.introVisible = false }
    }

    Keys.onPressed: event => {
        event.accepted = true
        if (!event.isAutoRepeat && !intro.leaving) intro.skip()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: if (!intro.leaving) intro.skip()
    }
}
