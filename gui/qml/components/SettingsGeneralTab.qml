import QtQuick
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    readonly property int controlWidth: 420

    SettingsCard {
        Layout.fillWidth: true

        SettingsRow {
            Layout.fillWidth: true
            label: "Game language"
            description: "Applies when you next launch a game. Text and subtitles use languages included in the game."

            MenuComboBox {
                id: languageChoice
                objectName: "systemLanguageChoice"
                Layout.preferredWidth: tab.controlWidth
                Layout.maximumWidth: parent.width
                navigateDown: regionChoice
                model: ["Japanese", "English", "French", "German", "Italian", "Spanish",
                        "Chinese (simplified)", "Korean", "Dutch", "Portuguese", "Russian", "Chinese (traditional)"]
                currentIndex: EmulatorLauncher.systemLanguage
                onActivated: EmulatorLauncher.systemLanguage = currentIndex
            }
        }

        SettingsRow {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            label: "Console region"
            description: "Applies when you next launch a game."

            MenuComboBox {
                id: regionChoice
                objectName: "systemRegionChoice"
                Layout.preferredWidth: tab.controlWidth
                Layout.maximumWidth: parent.width
                navigateUp: languageChoice
                navigateDown: volumeSlider
                property var regionIds: [0, 1, 2, 4, 16, 32, 64]
                model: ["Automatic (game region)", "Japan", "North America", "Europe", "China", "Korea", "Taiwan"]
                currentIndex: regionIds.indexOf(EmulatorLauncher.systemRegion)
                onActivated: EmulatorLauncher.systemRegion = regionIds[currentIndex]
            }
        }

        SettingsRow {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            label: "Volume"
            description: "Applies straight away, including to a game that is already running."

            RowLayout {
                Layout.maximumWidth: tab.controlWidth
                spacing: Theme.space3

                MenuSlider {
                    id: volumeSlider
                    objectName: "audioVolumeSlider"
                    Layout.fillWidth: true
                    from: 0; to: 100; stepSize: 5
                    value: EmulatorLauncher.audioVolume
                    onMoved: EmulatorLauncher.audioVolume = Math.round(value)
                }

                Text {
                    Layout.preferredWidth: 64
                    text: EmulatorLauncher.audioVolume === 0 ? "Muted" : EmulatorLauncher.audioVolume + "%"
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
