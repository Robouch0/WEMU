import QtQuick
import QtQuick.Controls

Rectangle {
    id: page
    color: "#e8e8ed"
    anchors.fill: parent
    focus: true

    Component.onCompleted: page.forceActiveFocus()

    // Vertical focus: true = the header Settings button, false = the carousel.
    property bool settingsFocused: false
    // Left-stick vertical edge state: -1 = pushed up, +1 = pushed down.
    property int vDir: 0
    // True between launch and the game's first presented frame (RPX loading).
    property bool launching: false

    function launchCurrent() {
        if (launching) return
        if (carousel.count > 0 && carousel.currentItem)
            EmulatorLauncher.launch(carousel.currentItem.gameRpxPath,
                                    carousel.currentItem.gameName)
    }

    function activateCurrent() {
        if (settingsFocused)
            mainLoader.source = "pages/SettingsPage.qml"
        else
            launchCurrent()
    }

    // ───────────────────────────────────────────────────────────── header

    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64
        color: "#ffffff"

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#d0d0d5"
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: "WEMU"
            font.pixelSize: 24
            font.bold: true
            color: "#1a1a1a"
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Rectangle {
                visible: carousel.count > 0
                anchors.verticalCenter: parent.verticalCenter
                width: countText.width + 24
                height: 28
                radius: 14
                color: "#e4e4ea"

                Text {
                    id: countText
                    anchors.centerIn: parent
                    text: carousel.count + (carousel.count === 1 ? " game" : " games")
                    color: "#444444"
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 110
                height: 36
                radius: 6
                color: settingsMouseArea.containsMouse || page.settingsFocused ? "#d0d0d5" : "#e4e4ea"
                border.color: page.settingsFocused ? "#3498ff" : "transparent"
                border.width: page.settingsFocused ? 2 : 0
                Behavior on color { ColorAnimation { duration: 100 } }
                Behavior on border.color { ColorAnimation { duration: 120 } }

                Text {
                    anchors.centerIn: parent
                    text: "Settings ▸"
                    color: "#1a1a1a"
                    font.pixelSize: 13
                    font.bold: true
                }

                MouseArea {
                    id: settingsMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mainLoader.source = "pages/SettingsPage.qml"
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────── empty state

    Item {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: carousel.count === 0

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 520)
            height: 220
            radius: 14
            color: "#ffffff"
            border.color: "#d0d0d5"
            border.width: 1

            Column {
                anchors.centerIn: parent
                spacing: 14
                width: parent.width - 48

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "No games found"
                    font.pixelSize: 22
                    font.bold: true
                    color: "#1a1a1a"
                }

                Text {
                    width: parent.width
                    text: "Drop a game folder (containing code/*.rpx and meta/meta.xml) into:"
                    color: "#666666"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(pathText.implicitWidth + 24, parent.width)
                    height: pathText.implicitHeight + 14
                    radius: 4
                    color: "#f0f0f3"
                    border.color: "#d0d0d5"

                    Text {
                        id: pathText
                        anchors.centerIn: parent
                        text: TitleScanner.searchPath
                        font.family: "monospace"
                        font.pixelSize: 12
                        color: "#444444"
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }

    // ───────────────────────────────────────────────────────────── carousel

    ListView {
        id: carousel
        anchors.top: header.bottom
        anchors.topMargin: 50
        anchors.left: parent.left
        anchors.right: parent.right
        height: 300
        visible: count > 0

        orientation: ListView.Horizontal
        snapMode: ListView.SnapToItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: width / 2 - 130
        preferredHighlightEnd: width / 2 + 130
        spacing: 30
        clip: true

        model: TitleScanner

        delegate: Item {
            id: cardWrapper
            width: 240
            height: 280
            // Visually selected only while the carousel itself has focus
            // the highlight follows the focus up to the Settings button.
            property bool isCurrent: ListView.isCurrentItem && !page.settingsFocused
            property string gameName:      model.name !== "" ? model.name : "Unknown"
            property string gamePublisher: model.publisher
            property string gameVersion:   model.version
            property string gameRpxPath:   model.rpxPath
            property string gameIconPath:  model.iconPath

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: 240
                height: 260
                radius: 14
                scale: cardWrapper.isCurrent ? 1.06 : 0.92
                color: "#ffffff"
                border.color: cardWrapper.isCurrent ? "#3498ff" : "#d0d0d5"
                border.width: cardWrapper.isCurrent ? 3 : 1

                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Behavior on border.color { ColorAnimation { duration: 150 } }
                Behavior on border.width { NumberAnimation { duration: 150 } }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (carousel.currentIndex === index)
                            page.launchCurrent()
                        else
                            carousel.currentIndex = index
                    }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Rectangle {
                        width: parent.width
                        height: parent.height - nameText.implicitHeight - parent.spacing
                        color: "#f0f0f3"
                        radius: 8
                        border.color: "#e0e0e3"
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: cardWrapper.gameIconPath
                            fillMode: Image.PreserveAspectCrop
                            visible: source.toString() !== ""
                            asynchronous: true
                        }
                    }

                    Text {
                        id: nameText
                        width: parent.width
                        text: cardWrapper.gameName
                        color: "#1a1a1a"
                        font.pixelSize: 15
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ──────────────────────────────────────────────────────── selected info

    Column {
        visible: carousel.count > 0
        anchors.top: carousel.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 28
        spacing: 14
        width: parent.width - 80

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (!carousel.currentItem) return ""
                var pub = carousel.currentItem.gamePublisher
                var ver = carousel.currentItem.gameVersion
                if (pub && ver) return pub + " · v" + ver
                if (pub) return pub
                if (ver) return "v" + ver
                return ""
            }
            color: "#666666"
            font.pixelSize: 14
            visible: text !== ""
        }

        Rectangle {
            id: launchBtn
            anchors.horizontalCenter: parent.horizontalCenter
            width: 240
            height: 56
            radius: 10
            color: page.launching ? "#9aa0a6"
                 : launchMouseArea.containsMouse ? "#2980d9" : "#3498ff"
            Behavior on color { ColorAnimation { duration: 120 } }

            Row {
                anchors.centerIn: parent
                spacing: 10

                BusyIndicator {
                    visible: page.launching
                    running: visible
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.launching ? "Loading game RPX…" : "▶  Play"
                    color: "white"
                    font.pixelSize: 18
                    font.bold: true
                }
            }

            MouseArea {
                id: launchMouseArea
                anchors.fill: parent
                hoverEnabled: true
                enabled: !page.launching
                cursorShape: Qt.PointingHandCursor
                onClicked: page.launchCurrent()
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "← →  navigate     ↑  settings     A / Enter  select"
            color: "#999999"
            font.pixelSize: 11
        }
    }

    // ─────────────────────────────────────────────────────── keyboard nav

    Keys.onLeftPressed: if (!settingsFocused && carousel.count > 0) carousel.decrementCurrentIndex()
    Keys.onRightPressed: if (!settingsFocused && carousel.count > 0) carousel.incrementCurrentIndex()
    Keys.onUpPressed: settingsFocused = true
    Keys.onDownPressed: settingsFocused = false
    Keys.onReturnPressed: page.activateCurrent()
    Keys.onEnterPressed: page.activateCurrent()

    // ─────────────────────────────────────────────────────── controller nav

    // Left-stick state: -1 = pushed left, +1 = pushed right, 0 = centered.
    property int stickDir: 0

    function stickStep() {
        if (!rootWindow.visible || carousel.count === 0 || settingsFocused) {
            stickRepeat.stop()
            stickDir = 0
            return
        }
        if (stickDir < 0)      carousel.decrementCurrentIndex()
        else if (stickDir > 0) carousel.incrementCurrentIndex()
    }

    Timer {
        id: stickRepeat
        repeat: true
        onTriggered: {
            interval = 140
            page.stickStep()
        }
    }

    Connections {
        target: InputManager

        function onButtonChanged(button, pressed, device) {
            if (!rootWindow.visible || !pressed) return
            if (button === "Up")          page.settingsFocused = true
            else if (button === "Down")   page.settingsFocused = false
            else if (button === "Left"  && !page.settingsFocused)  carousel.decrementCurrentIndex()
            else if (button === "Right" && !page.settingsFocused)  carousel.incrementCurrentIndex()
            else if (button === "A")      page.activateCurrent()
        }

        function onAxisChanged(axis, value, device) {
            if (!rootWindow.visible) return

            if (axis === "LX") {
                if (page.stickDir === 0 && Math.abs(value) >= 0.5 && !page.settingsFocused) {
                    page.stickDir = value < 0 ? -1 : 1
                    page.stickStep()
                    stickRepeat.interval = 350
                    stickRepeat.restart()
                } else if (page.stickDir !== 0 && Math.abs(value) < 0.25) {
                    page.stickDir = 0
                    stickRepeat.stop()
                }
            } else if (axis === "LY") {
                if (page.vDir === 0 && Math.abs(value) >= 0.5) {
                    page.vDir = value < 0 ? -1 : 1
                    page.settingsFocused = (page.vDir === -1)
                } else if (page.vDir !== 0 && Math.abs(value) < 0.25) {
                    page.vDir = 0
                }
            }
        }
    }

    Connections {
        target: EmulatorLauncher
        // Hide only once the game's first frame is on screen the menu stays visible during the RPX load instead of leaving no window at all
        function onRendererReady() {
            rootWindow.visible = false
            page.launching = false
        }
        function onStateChanged(running) {
            if (running) {
                page.launching = true
            } else {
                page.launching = false
                rootWindow.visible = true
            }
        }
    }
}
