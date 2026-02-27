import QtQuick
import QtQuick.Controls
import QtMultimedia

Rectangle {
    id: root
    width: 1200
    height: 900

    signal introFinished()
    property bool introFinishedFlag: false

    Row {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            SequentialAnimation on color {
                running: !root.introFinishedFlag
                loops: 1
                ColorAnimation { from: "white"; to: "grey"; duration: 1000; easing.type: Easing.InOutQuad }
                ColorAnimation { from: "grey"; to: "white"; duration: 1000; easing.type: Easing.InOutQuad }
                ColorAnimation { from: "white"; to: "grey"; duration: 900;  easing.type: Easing.InOutQuad }
                ColorAnimation { from: "grey"; to: "white"; duration: 900;  easing.type: Easing.InOutQuad }
                ColorAnimation { from: "white"; to: "blue";  duration: 800;  easing.type: Easing.InOutQuad }
            }
            width: parent.width / 3
            height: parent.height
        }

        Rectangle {
            id: middleColumn
            width: parent.width / 3
            height: parent.height
            color: "white"

            SequentialAnimation on color {
                running: !root.introFinishedFlag
                loops: 1
                ColorAnimation {
                    from: "white";
                    to: "white"; duration: 1000; easing.type: Easing.InOutQuad
                }
                ColorAnimation {
                    from: "white";
                    to: "white"; duration: 1000; easing.type: Easing.InOutQuad
                }
            }

            Rectangle {
                id: videoCircle
                width: Math.min(parent.width, parent.height) * 0.5
                height: width
                color: "transparent"
                radius: width * 0.3
                border.color: "transparent"
                anchors.centerIn: parent

                MediaPlayer {
                    id: introVideo
                    source: "qrc:/assets/qml/assets/FrenchGuy.mp4"
                    autoPlay: true
                    loops: 1
                    videoOutput: videoOutput

                    onPlaybackStateChanged: {
                        if (playbackState === MediaPlayer.StoppedState) {
                            console.log("Video finished.")
                            root.introFinishedFlag = true
                            root.introFinished()
                        }
                    }

                    onErrorOccurred: console.log("Video error:", errorString)
                }

                VideoOutput {
                    id: videoOutput
                    anchors.fill: parent
                    fillMode: VideoOutput.PreserveAspectCrop
                }
            }
        }

            Rectangle {
            SequentialAnimation on color {
                running: !root.introFinishedFlag
                loops: 1
                ColorAnimation { from: "white"; to: "grey"; duration: 1000; easing.type: Easing.InOutQuad }
                ColorAnimation { from: "grey"; to: "white"; duration: 1000; easing.type: Easing.InOutQuad }
                ColorAnimation { from: "white"; to: "grey"; duration: 500;  easing.type: Easing.InOutQuad }
                ColorAnimation { from: "grey"; to: "white"; duration: 500;  easing.type: Easing.InOutQuad }
                ColorAnimation { from: "white"; to: "red";  duration: 1700; easing.type: Easing.InOutQuad }
                ScriptAction { script: { root.introFinishedFlag = true; root.introFinished(); } }
            }
            width: parent.width / 3
            height: parent.height
        }
    }
}
