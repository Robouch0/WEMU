import QtQuick
import QtQuick.Controls
import Wemu 1.0
import "../components"
import "../components/Theme.js" as Theme

Item {
    id: page
    visible: EmulatorLauncher.running
    focus: visible
    property int selection: 0
    property bool confirmingQuit: false
    property bool wasPaused: false

    onVisibleChanged: if (visible)
        forceActiveFocus()

    function activateSelection() {
        if (!EmulatorLauncher.paused || EmulatorLauncher.pausePending || EmulatorLauncher.stopping)
            return;
        if (confirmingQuit) {
            if (selection === 0) {
                confirmingQuit = false;
                selection = 2;
            } else
                EmulatorLauncher.stop();
        } else if (selection === 0)
            EmulatorLauncher.resume();
        else if (selection === 1)
            EmulatorLauncher.toggleFps();
        else {
            confirmingQuit = true;
            selection = 0;
        }
    }

    Keys.onPressed: function (event) {
        if (event.isAutoRepeat) {
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Escape) {
            if (confirmingQuit) {
                confirmingQuit = false;
                selection = 2;
            } else
                EmulatorLauncher.togglePause();
        } else if (EmulatorLauncher.paused && !EmulatorLauncher.pausePending) {
            var count = confirmingQuit ? 2 : 3;
            if (event.key === Qt.Key_Up)
                selection = (selection + count - 1) % count;
            else if (event.key === Qt.Key_Down)
                selection = (selection + 1) % count;
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                activateSelection();
        }
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
    GameView {
        id: gameImage
        objectName: "gameView"
        anchors.fill: parent
    }

    MouseArea {
        anchors.fill: parent
        onClicked: page.forceActiveFocus()
    }

    Rectangle {
        visible: EmulatorLauncher.showFps && EmulatorLauncher.hasFrame && !EmulatorLauncher.paused
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 16
        width: fpsText.implicitWidth + 28
        height: fpsText.implicitHeight + 20
        radius: 6
        color: "#dd151b25"
        Text {
            id: fpsText
            anchors.centerIn: parent
            color: "#a4f2bf"
            font.pixelSize: 15
            font.family: "monospace"
            text: "PRESENT FPS: " + (EmulatorLauncher.presentFps < 0 ? "--" : EmulatorLauncher.presentFps.toFixed(1)) + "\nMOVIE FPS:   " + (EmulatorLauncher.movieFps < 0 ? "--" : EmulatorLauncher.movieFps.toFixed(1))
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !EmulatorLauncher.hasFrame && !EmulatorLauncher.paused
        color: "#151b25"
        Column {
            anchors.centerIn: parent
            spacing: 20
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Starting " + EmulatorLauncher.gameTitle
                color: Theme.panelText
                font.pixelSize: 24
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: parent.parent.visible
            }
            SessionMenuButton {
                width: 240
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Return to library"
                onClicked: EmulatorLauncher.stop()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: EmulatorLauncher.paused || EmulatorLauncher.pausePending || EmulatorLauncher.stopping
        color: "#99000000"
        MouseArea {
            anchors.fill: parent
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(500, parent.width - 48)
            height: menuColumn.implicitHeight + 56
            radius: 16
            color: Theme.panel
            border.color: "#485970"
            border.width: 1
            Column {
                id: menuColumn
                anchors.centerIn: parent
                width: parent.width - 56
                spacing: 14
                Text {
                    width: parent.width
                    color: Theme.panelText
                    font.pixelSize: 28
                    font.bold: true
                    text: EmulatorLauncher.stopping ? "Returning to library…" : EmulatorLauncher.pausePending ? (EmulatorLauncher.paused ? "Resuming…" : "Pausing…") : page.confirmingQuit ? "Return to library?" : "Game paused"
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 14
                    text: page.confirmingQuit ? "The game will stop. Unsaved progress will be lost." : "O or Escape to resume."
                }
                Repeater {
                    model: page.confirmingQuit ? ["Cancel", "Stop and return to library"] : ["Resume", "Show FPS: " + (EmulatorLauncher.showFps ? "On" : "Off"), "Return to library"]
                    SessionMenuButton {
                        width: menuColumn.width
                        text: modelData
                        highlighted: index === page.selection
                        enabled: EmulatorLauncher.paused && !EmulatorLauncher.pausePending && !EmulatorLauncher.stopping
                        onClicked: {
                            page.selection = index;
                            page.activateSelection();
                        }
                    }
                }
                Text {
                    width: parent.width
                    color: Theme.mutedText
                    font.pixelSize: 12
                    text: "↑ / ↓ select     Enter accept"
                }
            }
        }
    }

    Connections {
        target: EmulatorLauncher
        function onSessionChanged() {
            if (EmulatorLauncher.paused && !page.wasPaused) {
                page.selection = 0;
                page.confirmingQuit = false;
            }
            if (!EmulatorLauncher.paused)
                page.confirmingQuit = false;
            page.wasPaused = EmulatorLauncher.paused;
        }
    }
}
