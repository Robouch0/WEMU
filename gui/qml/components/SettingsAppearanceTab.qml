import QtQuick
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    readonly property var accentLabels: ({
        blue: qsTr("Blue"), teal: qsTr("Teal"), green: qsTr("Green"), amber: qsTr("Amber"),
        orange: qsTr("Orange"), red: qsTr("Red"), pink: qsTr("Pink"), purple: qsTr("Purple")
    })
    property var artwork: TitleScanner.artworkPaths(12)
    // Whichever swatch has focus is named, so a color can be identified before choosing it.
    property string shownAccent: AppPreferences.accentColor
    readonly property Item chosenCard: AppPreferences.themeMode === "dark" ? darkCard : lightCard
    readonly property Item chosenSwatch: swatches.count > 0 ? swatches.itemAt(Theme.accentNames.indexOf(AppPreferences.accentColor)) : null

    Connections {
        target: TitleScanner
        function onModelReset() { tab.artwork = TitleScanner.artworkPaths(12) }
    }

    Connections {
        target: AppPreferences
        function onChanged() { tab.shownAccent = AppPreferences.accentColor }
    }

    SettingsCard {
        Layout.fillWidth: true

        Text {
            text: qsTr("Theme")
            font.pixelSize: Theme.fontBody
            font.bold: true
            color: Theme.text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space4

            ThemeCard {
                id: lightCard
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                mode: "light"
                text: qsTr("Light")
                artwork: tab.artwork
                KeyNavigation.right: darkCard
                KeyNavigation.down: tab.chosenSwatch
            }

            ThemeCard {
                id: darkCard
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                mode: "dark"
                text: qsTr("Dark")
                artwork: tab.artwork
                KeyNavigation.left: lightCard
                KeyNavigation.down: tab.chosenSwatch
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            implicitHeight: 1
            color: Theme.separator
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            Text {
                text: qsTr("Accent color")
                font.pixelSize: Theme.fontBody
                font.bold: true
                color: Theme.text
            }

            Text {
                Layout.fillWidth: true
                text: tab.accentLabels[tab.shownAccent]
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
            }

            Row {
                spacing: Theme.space1

                Repeater {
                    id: swatches
                    model: Theme.accentNames

                    AccentSwatch {
                        required property string modelData
                        required property int index
                        colorName: modelData
                        KeyNavigation.left: index > 0 ? swatches.itemAt(index - 1) : null
                        KeyNavigation.right: index + 1 < swatches.count ? swatches.itemAt(index + 1) : null
                        KeyNavigation.up: tab.chosenCard
                        KeyNavigation.down: fullscreenToggle.control
                        onActiveFocusChanged: tab.shownAccent = activeFocus ? colorName : AppPreferences.accentColor
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            implicitHeight: 1
            color: Theme.separator
        }

        SettingsToggleRow {
            id: fullscreenToggle
            Layout.fillWidth: true
            label: qsTr("Fullscreen")
            description: qsTr("Fills the screen and keeps each game's proportions. F11 also switches it.")
            upTarget: tab.chosenSwatch
            checked: AppPreferences.fullscreen
            onToggled: AppPreferences.fullscreen = checked
        }
    }
}
