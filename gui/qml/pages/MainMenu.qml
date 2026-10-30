import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import gui 1.0

Rectangle {
    id: page
    color: Theme.bg
    anchors.fill: parent

    readonly property Item selected: grid.currentItem
    readonly property int panelWidth: Math.max(280, Math.min(420, width * 0.3))

    function focusContent() {
        if (grid.count > 0) grid.forceActiveFocus()
        else chooseFolder.forceActiveFocus()
    }

    function launchCurrent() {
        if (EmulatorLauncher.running || !selected) return
        EmulatorLauncher.launch(selected.gameRpxPath, selected.gameName, selected.gameContentPath)
    }

    function openSettings(tab) {
        rootWindow.settingsTab = tab
        rootWindow.libraryIndex = grid.currentIndex
        mainLoader.source = "pages/SettingsPage.qml"
    }

    function selectIndex(index) {
        grid.currentIndex = Math.max(0, Math.min(index, grid.count - 1))
    }

    function formatSize(bytes) {
        const units = ["KB", "MB", "GB", "TB"]
        let value = bytes / 1024
        let unit = 0
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024
            unit++
        }
        return (value >= 100 ? value.toFixed(0) : value.toFixed(1)) + " " + units[unit]
    }

    onSelectedChanged: panelEnter.restart()

    OpacityAnimator on opacity { from: 0; to: 1; duration: Theme.durationNormal }

    Component.onCompleted: {
        selectIndex(rootWindow.libraryIndex)
        // Wait for the view's size. Positioning helpers ignore the top margin, so the first row is set directly.
        Qt.callLater(() => {
            if (grid.currentIndex < grid.columns) grid.contentY = -grid.topMargin
            else grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        })
        focusContent()
    }

    FolderDialog {
        id: libraryDialog
        title: "Game library folder"
        onAccepted: TitleScanner.scanDirectory(selectedFolder.toString())
    }

    // ───────────────────────────────────────────────────────────── header

    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.controlHeight + 2 * Theme.space3
        color: Theme.surface
        z: 1

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.separator
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space5
            anchors.rightMargin: Theme.space5
            spacing: Theme.space3

            Text {
                text: "WEMU"
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.text
            }

            Text {
                Layout.leftMargin: Theme.space2
                visible: grid.count > 0
                text: grid.count + (grid.count === 1 ? " game" : " games")
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
            }

            Item { Layout.fillWidth: true }

            RemoteGamepadBadge {
                id: phoneButton
                KeyNavigation.right: folderButton
                onClicked: page.openSettings(2)
            }

            MenuButton {
                id: folderButton
                soft: true
                iconPath: Theme.iconFolder
                text: "Games folder"
                KeyNavigation.left: phoneButton
                KeyNavigation.right: settingsButton
                onClicked: libraryDialog.open()
            }

            MenuButton {
                id: settingsButton
                soft: true
                iconPath: Theme.iconSettings
                text: "Settings"
                KeyNavigation.left: folderButton
                onClicked: page.openSettings(0)
            }
        }

        // Keys left unhandled by a header button return to the games.
        Keys.onDownPressed: page.focusContent()
        Keys.onEscapePressed: page.focusContent()
    }

    // ─────────────────────────────────────────────────────────── empty state

    Item {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: grid.count === 0

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.space7, 520)
            spacing: Theme.space3

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "No games found"
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.text
            }

            Text {
                Layout.fillWidth: true
                text: TitleScanner.searchPath === "" ? "Choose the folder that holds your Wii U games."
                                                     : "This folder has no games or RPX demos. Choose another one."
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: TitleScanner.searchPath !== ""
                text: TitleScanner.searchPath
                font.pixelSize: Theme.fontCaption
                font.family: "monospace"
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
            }

            MenuButton {
                id: chooseFolder
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.space2
                primary: true
                text: "Choose folder"
                KeyNavigation.up: settingsButton
                onClicked: libraryDialog.open()
            }
        }
    }

    // ───────────────────────────────────────────────────────── details panel

    Rectangle {
        id: panel
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: page.panelWidth
        color: Theme.surface
        visible: grid.count > 0

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.separator
        }

        ParallelAnimation {
            id: panelEnter
            NumberAnimation { target: panelContent; property: "opacity"; from: 0.5; to: 1; duration: Theme.durationNormal }
            NumberAnimation { target: panelShift; property: "x"; from: -Theme.space2; to: 0; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            id: panelContent
            anchors.fill: parent
            anchors.margins: Theme.space5
            spacing: Theme.space2
            transform: Translate { id: panelShift }

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: Theme.space2
                readonly property int side: Math.min(256, panel.width - 2 * Theme.space5)
                Layout.preferredWidth: side
                Layout.preferredHeight: side
                radius: Theme.radiusMedium
                color: Theme.surfaceMuted
                clip: true

                Image {
                    id: panelArt
                    anchors.fill: parent
                    source: page.selected ? page.selected.gameIconPath : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                }

                Text {
                    anchors.centerIn: parent
                    visible: panelArt.status !== Image.Ready
                    text: page.selected ? page.selected.gameName.charAt(0).toUpperCase() : ""
                    font.pixelSize: 96
                    font.bold: true
                    color: Theme.textMuted
                }
            }

            Text {
                Layout.fillWidth: true
                text: page.selected ? page.selected.gameName : ""
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.text
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: page.selected ? page.selected.gamePublisher : ""
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: {
                    if (!page.selected) return ""
                    const size = page.selected.gameSize
                    const parts = page.selected.gameVersion !== "" ? ["Version " + page.selected.gameVersion] : []
                    parts.push(size < 0 ? "Measuring size…" : page.formatSize(size))
                    return parts.join(" · ")
                }
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: "Controls: Profile " + (InputProfileManager.currentProfileIndex + 1)
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                elide: Text.ElideRight
            }

            MenuButton {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space3
                Layout.preferredHeight: 52
                primary: true
                focusPolicy: Qt.NoFocus
                font.pixelSize: Theme.fontSubtitle
                text: "▶  Play"
                onClicked: page.launchCurrent()
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                visible: text !== ""
                text: EmulatorLauncher.error
                font.pixelSize: Theme.fontCaption
                color: Theme.danger
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            MenuButton {
                id: logButton
                Layout.fillWidth: true
                visible: EmulatorLauncher.logPath !== ""
                text: "Session log"
                KeyNavigation.right: grid
                onClicked: EmulatorLauncher.openLog()
            }

            Item { Layout.fillHeight: true }

            Text {
                Layout.fillWidth: true
                text: grid.activeFocus ? "A play · Up from the top row for the menu"
                                       : "A select · B back to games"
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    // ────────────────────────────────────────────────────────────── grid

    GridView {
        id: grid
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: panel.right
        anchors.right: parent.right
        visible: count > 0
        clip: true
        model: TitleScanner

        readonly property int gap: Theme.space4
        readonly property int minimumTile: 180
        readonly property int availableWidth: width - 2 * Theme.space5
        readonly property int columns: Math.max(2, Math.floor((availableWidth + gap) / (minimumTile + gap)))

        // Each cell carries its trailing gap, so the right margin gives that much back.
        leftMargin: Theme.space5
        rightMargin: Theme.space5 - gap
        topMargin: Theme.space5
        bottomMargin: Theme.space5
        cellWidth: Math.floor((availableWidth + gap) / columns)
        cellHeight: cellWidth + Theme.space6 + Theme.space1
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightMoveDuration: Theme.durationFast
        highlightRangeMode: GridView.ApplyRange
        preferredHighlightBegin: Theme.space5
        preferredHighlightEnd: height - Theme.space5
        cacheBuffer: 2 * cellHeight

        // A rescan resets the model; keep the selection on the same row when it can.
        property int indexBeforeReset: 0
        onCurrentIndexChanged: if (count > 0) indexBeforeReset = currentIndex
        onCountChanged: Qt.callLater(() => {
            if (grid.count > 0) {
                page.selectIndex(grid.indexBeforeReset)
            } else if (grid.activeFocus) {
                chooseFolder.forceActiveFocus()
            }
        })

        Keys.onUpPressed: {
            if (currentIndex < columns) settingsButton.forceActiveFocus()
            else currentIndex -= columns
        }
        Keys.onDownPressed: {
            if (currentIndex + columns < count) currentIndex += columns
            else if (Math.floor(currentIndex / columns) < Math.floor((count - 1) / columns)) currentIndex = count - 1
        }
        Keys.onLeftPressed: {
            if (currentIndex % columns > 0) currentIndex--
            else if (logButton.visible) logButton.forceActiveFocus()
        }
        Keys.onRightPressed: {
            if (currentIndex % columns < columns - 1 && currentIndex + 1 < count) currentIndex++
        }
        Keys.onPressed: event => {
            if (event.key !== Qt.Key_Space && event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter) return
            if (!event.isAutoRepeat) page.launchCurrent()
            event.accepted = true
        }

        delegate: Item {
            id: tile
            width: grid.cellWidth
            height: grid.cellHeight

            readonly property bool isCurrent: GridView.isCurrentItem
            readonly property bool focused: isCurrent && grid.activeFocus
            readonly property string gameName: model.name !== "" ? model.name : "Unknown"
            readonly property string gamePublisher: model.publisher
            readonly property string gameVersion: model.version
            readonly property string gameRpxPath: model.rpxPath
            readonly property string gameContentPath: model.contentPath
            readonly property string gameIconPath: model.iconPath
            readonly property real gameSize: model.sizeBytes

            Rectangle {
                id: card
                anchors.fill: parent
                anchors.rightMargin: grid.gap
                anchors.bottomMargin: grid.gap
                radius: Theme.radiusMedium
                color: tileMouse.containsMouse && !tile.isCurrent ? Theme.surfaceMuted : Theme.surface
                border.color: tile.isCurrent ? Theme.borderControl : Theme.separator
                scale: tile.focused ? 1.05 : tileMouse.pressed ? 0.98 : 1
                z: tile.focused ? 1 : 0
                Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack } }
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                FocusRing {
                    shown: tile.focused
                    cornerRadius: Theme.radiusMedium
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    spacing: Theme.space2

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusSmall
                        color: Theme.surfaceMuted
                        clip: true

                        Image {
                            id: tileArt
                            anchors.fill: parent
                            source: tile.gameIconPath
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: tileArt.status !== Image.Ready
                            text: tile.gameName.charAt(0).toUpperCase()
                            font.pixelSize: Theme.fontDisplay
                            font.bold: true
                            color: Theme.textMuted
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: tile.gameName
                        font.pixelSize: Theme.fontBody
                        font.bold: tile.focused
                        color: Theme.text
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }

            MouseArea {
                id: tileMouse
                anchors.fill: card
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Only a pointer that really moved selects: not one resting where the window opened,
                // and not one with tiles scrolling under it.
                onPositionChanged: mouse => {
                    const scene = mapToItem(null, mouse.x, mouse.y)
                    const moved = grid.lastPointer.x >= 0 && (scene.x !== grid.lastPointer.x || scene.y !== grid.lastPointer.y)
                    grid.lastPointer = scene
                    if (moved) grid.currentIndex = index
                }
                onClicked: {
                    grid.currentIndex = index
                    grid.forceActiveFocus()
                }
                onDoubleClicked: page.launchCurrent()
            }
        }

        property point lastPointer: Qt.point(-1, -1)
    }

    Connections {
        target: EmulatorLauncher
        // Keep the grid and its selection while the isolated core fills the game view.
        function onStateChanged(running) {
            if (running) return
            rootWindow.visible = true
            rootWindow.raise()
            rootWindow.requestActivate()
            page.focusContent()
        }
    }
}
