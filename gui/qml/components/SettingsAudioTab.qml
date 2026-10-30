import QtQuick
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    SettingsCard {
        Layout.fillWidth: true

        SettingsRow {
            Layout.fillWidth: true
            label: qsTr("Volume")
            description: qsTr("Applies straight away, including to a game that is already running.")

            RowLayout {
                Layout.maximumWidth: 420
                spacing: Theme.space3

                MenuSlider {
                    objectName: "audioVolumeSlider"
                    Layout.fillWidth: true
                    from: 0; to: 100; stepSize: 5
                    value: EmulatorLauncher.audioVolume
                    onMoved: EmulatorLauncher.audioVolume = Math.round(value)
                }

                Text {
                    Layout.preferredWidth: 64
                    text: EmulatorLauncher.audioVolume === 0 ? qsTr("Muted") : EmulatorLauncher.audioVolume + "%"
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
