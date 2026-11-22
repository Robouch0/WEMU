import QtQuick
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    readonly property int controlWidth: 420
    readonly property string activeTest: MenuAudio.testPlaying ? playingTest : ""
    property string playingTest: ""
    // Where Left returns to from the sound test.
    property Item returnTarget: musicToggle.control

    function enterSoundTest(from) {
        returnTarget = from
        lowButton.forceActiveFocus()
    }

    function startTest(name) {
        playingTest = name
        MenuAudio.playTest(name)
    }

    Binding {
        target: MenuAudio
        property: "spectrumActive"
        value: tab.visible
    }

    component VolumeRow: SettingsRow {
        id: row

        property alias value: slider.value
        property alias sliderName: slider.objectName
        property string valueText: ""
        signal moved()

        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: tab.controlWidth
            spacing: Theme.space3

            MenuSlider {
                id: slider
                Layout.fillWidth: true
                from: 0; to: 100; stepSize: 5
                onMoved: row.moved()
                // A new press at full volume crosses over; holding Right up to 100 stays here.
                Keys.onRightPressed: event => {
                    if (value >= to && !event.isAutoRepeat) tab.enterSoundTest(slider)
                    else event.accepted = false
                }
            }

            Text {
                Layout.preferredWidth: 72
                text: row.valueText
                font.pixelSize: Theme.fontBody
                color: Theme.text
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width >= 880 ? 2 : 1
        columnSpacing: Theme.space4
        rowSpacing: Theme.space4

        SettingsCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop

            VolumeRow {
                Layout.fillWidth: true
                label: qsTr("Game volume")
                description: qsTr("Applies straight away, including to a game that is already running.")
                sliderName: "audioVolumeSlider"
                value: EmulatorLauncher.audioVolume
                valueText: EmulatorLauncher.audioVolume === 0 ? qsTr("Muted") : EmulatorLauncher.audioVolume + "%"
                onMoved: EmulatorLauncher.audioVolume = Math.round(value)
            }

            VolumeRow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                label: qsTr("Menu volume")
                description: qsTr("Sounds and music in WEMU's menus. Games are not affected.")
                sliderName: "menuVolumeSlider"
                value: MenuAudio.volume
                valueText: MenuAudio.volume === 0 ? qsTr("Muted") : MenuAudio.volume + "%"
                onMoved: {
                    MenuAudio.volume = Math.round(value)
                    MenuAudio.play("move")
                }
            }

            SettingsToggleRow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                id: musicToggle
                label: qsTr("Menu music")
                rightTarget: lowButton
                onControlFocused: tab.returnTarget = control
                description: qsTr("A quiet looping track behind the menus. It stops while a game runs.")
                checked: MenuAudio.musicEnabled
                onToggled: MenuAudio.musicEnabled = checked
            }

            Item { Layout.fillHeight: true }
        }

        SettingsCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            title: qsTr("Sound test")

            Text {
                Layout.fillWidth: true
                text: !MenuAudio.available ? qsTr("No audio output is available.")
                    : MenuAudio.volume === 0 ? qsTr("Menu volume is muted, so the test will be silent.")
                    : qsTr("Plays a 5 second sound at the menu volume. The graph shows what WEMU sends to your speakers.")
                font.pixelSize: Theme.fontCaption
                color: !MenuAudio.available || MenuAudio.volume === 0 ? Theme.warning : Theme.textMuted
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3

                MenuButton {
                    id: lowButton
                    Layout.fillWidth: true
                    clickSound: ""
                    enabled: MenuAudio.available
                    primary: tab.activeTest === "low"
                    KeyNavigation.right: highButton
                    KeyNavigation.up: musicToggle.control
                    Keys.onLeftPressed: tab.returnTarget.forceActiveFocus()
                    Keys.onDownPressed: {}
                    soft: !primary
                    text: tab.activeTest === "low" ? qsTr("Stop") : qsTr("Low sound")
                    onClicked: tab.activeTest === "low" ? MenuAudio.stopTest() : tab.startTest("low")
                }

                MenuButton {
                    id: highButton
                    Layout.fillWidth: true
                    clickSound: ""
                    enabled: MenuAudio.available
                    primary: tab.activeTest === "high"
                    KeyNavigation.left: lowButton
                    KeyNavigation.up: musicToggle.control
                    Keys.onDownPressed: {}
                    soft: !primary
                    text: tab.activeTest === "high" ? qsTr("Stop") : qsTr("High sound")
                    onClicked: tab.activeTest === "high" ? MenuAudio.stopTest() : tab.startTest("high")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 4
                radius: 2
                color: Theme.surfaceMuted

                Rectangle {
                    width: parent.width * MenuAudio.testProgress
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                }
            }

            SpectrumBars {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 260
                Layout.topMargin: Theme.space2
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Low")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }

                Text {
                    text: qsTr("High")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }
            }
        }
    }
}
