import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    readonly property int controlWidth: 420
    readonly property int region: EmulatorLauncher.systemRegion
    readonly property var regionIds: [0, 1, 2, 4, 16, 32, 64]
    // Where the globe looks for each region, as latitude and longitude.
    readonly property var regionPlaces: [null, [36, 138], [40, -100], [50, 10], [35, 105], [36, 128], [24, 121]]

    function showRegion() {
        const place = regionPlaces[regionIds.indexOf(region)]
        if (place) globe.showPlace(place[0], place[1])
        else globe.showWholeWorld()
    }

    onRegionChanged: showRegion()
    Component.onCompleted: showRegion()

    // Translating replaces the lists below, and a new list puts a ComboBox back on its first entry.
    Connections {
        target: AppLanguage
        function onCodeChanged() {
            Qt.callLater(() => {
                languageChoice.currentIndex = EmulatorLauncher.systemLanguage
                regionChoice.currentIndex = tab.regionIds.indexOf(EmulatorLauncher.systemRegion)
            })
        }
    }

    FolderDialog {
        id: libraryDialog
        title: qsTr("Game library folder")
        onAccepted: TitleScanner.scanDirectory(selectedFolder.toString())
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width >= 880 ? 2 : 1
        columnSpacing: Theme.space4
        rowSpacing: Theme.space4

        SettingsCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("WEMU language")
                description: qsTr("The language of WEMU's menus and buttons.")

                MenuComboBox {
                    id: appLanguageChoice
                    objectName: "appLanguageChoice"
                    Layout.preferredWidth: tab.controlWidth
                    Layout.maximumWidth: parent.width
                    navigateDown: languageChoice
                    textRole: "name"
                    valueRole: "code"
                    model: AppLanguage.languages
                    currentIndex: AppLanguage.languages.findIndex(language => language.code === AppLanguage.code)
                    onActivated: AppLanguage.select(currentValue)
                }
            }

            SettingsRow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space1
                label: qsTr("Game language")
                description: qsTr("Applies when you next launch a game. Text and subtitles use languages included in the game.")

                MenuComboBox {
                    id: languageChoice
                    objectName: "systemLanguageChoice"
                    Layout.preferredWidth: tab.controlWidth
                    Layout.maximumWidth: parent.width
                    navigateUp: appLanguageChoice
                    navigateDown: regionChoice
                    model: [qsTr("Japanese"), qsTr("English"), qsTr("French"), qsTr("German"), qsTr("Italian"), qsTr("Spanish"),
                            qsTr("Chinese (simplified)"), qsTr("Korean"), qsTr("Dutch"), qsTr("Portuguese"), qsTr("Russian"), qsTr("Chinese (traditional)")]
                    currentIndex: EmulatorLauncher.systemLanguage
                    onActivated: EmulatorLauncher.systemLanguage = currentIndex
                }
            }

            SettingsRow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space1
                label: qsTr("Console region")
                description: qsTr("Applies when you next launch a game.")

                MenuComboBox {
                    id: regionChoice
                    objectName: "systemRegionChoice"
                    Layout.preferredWidth: tab.controlWidth
                    Layout.maximumWidth: parent.width
                    navigateUp: languageChoice
                    navigateDown: changeFolderButton
                    model: [qsTr("Automatic (game region)"), qsTr("Japan"), qsTr("North America"), qsTr("Europe"), qsTr("China"), qsTr("Korea"), qsTr("Taiwan")]
                    currentIndex: tab.regionIds.indexOf(EmulatorLauncher.systemRegion)
                    onActivated: EmulatorLauncher.systemRegion = tab.regionIds[currentIndex]
                }
            }

            SettingsRow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space1
                label: qsTr("Games folder")
                description: qsTr("WEMU shows the games found in this folder.")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3

                    Text {
                        Layout.fillWidth: true
                        text: TitleScanner.searchPath !== "" ? TitleScanner.searchPath : qsTr("No folder selected")
                        font.pixelSize: Theme.fontCaption
                        font.family: "monospace"
                        color: Theme.text
                        elide: Text.ElideMiddle
                    }

                    MenuButton {
                        id: changeFolderButton
                        soft: true
                        iconPath: Theme.iconFolder
                        text: qsTr("Change")
                        onClicked: libraryDialog.open()
                    }
                }
            }

            SettingsToggleRow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space1
                label: qsTr("Fullscreen")
                description: qsTr("Fills the screen and keeps each game's proportions. F11 also switches it.")
                checked: AppPreferences.fullscreen
                onToggled: AppPreferences.fullscreen = checked
            }

            SettingsToggleRow {
                Layout.fillWidth: true
                label: qsTr("Start on last played game")
                description: qsTr("Selects the game you played last when WEMU opens.")
                checked: AppPreferences.rememberLastGame
                onToggled: AppPreferences.rememberLastGame = checked
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            title: qsTr("Your console")

            DottedGlobe {
                id: globe
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 220
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Region")
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: -Theme.space2
                text: regionChoice.currentText
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
                color: Theme.text
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: -Theme.space2
                text: qsTr("Game language: %1").arg(languageChoice.currentText)
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}
