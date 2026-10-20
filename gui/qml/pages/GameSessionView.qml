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
    property int stickDir: 0

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

    function back() {
        if (confirmingQuit) {
            confirmingQuit = false;
            selection = 2;
        } else
            EmulatorLauncher.togglePause();
    }

    function moveSelection(step) {
        var count = confirmingQuit ? 2 : 3;
        selection = (selection + count + step) % count;
    }

    Keys.onPressed: function (event) {
        if (event.isAutoRepeat) {
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Escape)
            back();
        else if (EmulatorLauncher.paused && !EmulatorLauncher.pausePending) {
            if (event.key === Qt.Key_Up)
                moveSelection(-1);
            else if (event.key === Qt.Key_Down)
                moveSelection(1);
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                activateSelection();
        }
        event.accepted = true;
    }

    // Controllers: Guide opens and leaves this menu, the D-pad or left stick, A and B drive it. Their game input
    // goes through EmulatorLauncher.setControllerInput.
    Connections {
        target: InputManager
        function onButtonChanged(button, pressed, device) {
            if (!page.visible || !pressed)
                return;
            if (button === "XBOX_GUIDE")
                page.back();
            else if (!EmulatorLauncher.paused || EmulatorLauncher.pausePending)
                return;
            else if (button === "Up")
                page.moveSelection(-1);
            else if (button === "Down")
                page.moveSelection(1);
            else if (button === "A")
                page.activateSelection();
            else if (button === "B")
                page.back();
        }

        function onAxisChanged(axis, value, device) {
            if (!page.visible || axis !== "LY")
                return;
            if (page.stickDir === 0 && Math.abs(value) >= 0.5) {
                page.stickDir = value < 0 ? -1 : 1;
                if (EmulatorLauncher.paused && !EmulatorLauncher.pausePending)
                    page.moveSelection(page.stickDir);
            } else if (page.stickDir !== 0 && Math.abs(value) < 0.25)
                page.stickDir = 0;
        }
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

    // Mobile GamePad connect/disconnect toast.
    Rectangle {
        id: gamepadToast
        property string message: ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 24
        width: toastText.implicitWidth + 36
        height: 40
        radius: 20
        color: "#cc202632"
        opacity: 0
        visible: opacity > 0
        Text {
            id: toastText
            anchors.centerIn: parent
            text: gamepadToast.message
            color: "#f2f5fa"
            font.pixelSize: 14
            font.bold: true
        }
        SequentialAnimation {
            id: toastAnimation
            NumberAnimation { target: gamepadToast; property: "opacity"; to: 1; duration: 180 }
            PauseAnimation { duration: 2600 }
            NumberAnimation { target: gamepadToast; property: "opacity"; to: 0; duration: 400 }
        }
        Connections {
            target: RemoteGamepad
            property bool wasConnected: RemoteGamepad.clientConnected
            function onClientChanged() {
                if (RemoteGamepad.clientConnected === wasConnected)
                    return;
                wasConnected = RemoteGamepad.clientConnected;
                gamepadToast.message = wasConnected ? "GamePad connected · " + RemoteGamepad.clientName : "GamePad disconnected";
                toastAnimation.restart();
            }
        }
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
                    text: page.confirmingQuit ? "The game will stop. Unsaved progress will be lost." : "O, Escape or the controller's Guide button to resume."
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
                    text: "↑ / ↓ select     Enter or A accept"
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
