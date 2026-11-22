import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import gui 1.0

// A theme choice, shown as a small library in that theme with the player's own game art.
Basic.AbstractButton {
    id: card

    property string mode: ""
    property var artwork: []

    readonly property bool chosen: AppPreferences.themeMode === mode
    readonly property var neutrals: mode === "dark" ? Theme.darkNeutrals : Theme.lightNeutrals
    readonly property var accents: (mode === "dark" ? Theme.darkAccents : Theme.lightAccents)[AppPreferences.accentColor]

    implicitHeight: 248

    onClicked: {
        AppPreferences.themeMode = mode
        MenuAudio.play("confirm")
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    function art(index) { return artwork.length > 0 ? artwork[index % artwork.length] : "" }

    contentItem: ColumnLayout {
        spacing: Theme.space2

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: picture
                anchors.fill: parent
                radius: Theme.radiusMedium
                color: card.neutrals.bg
                clip: true

                Rectangle {
                    id: bar
                    width: parent.width
                    height: 28
                    color: card.neutrals.surface

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space3
                        anchors.verticalCenter: parent.verticalCenter
                        text: "WEMU"
                        font.pixelSize: 12
                        font.bold: true
                        color: card.neutrals.text
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space1

                        Repeater {
                            model: 3
                            Rectangle {
                                width: 28
                                height: 14
                                radius: 3
                                color: card.accents.soft
                            }
                        }
                    }
                }

                Rectangle {
                    id: side
                    anchors.top: bar.bottom
                    anchors.bottom: parent.bottom
                    width: Math.min(parent.width * 0.3, 140)
                    color: card.neutrals.surface

                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.space2
                        spacing: 6

                        Rectangle {
                            width: parent.width
                            height: width * 0.72
                            radius: 4
                            color: card.neutrals.surfaceMuted
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: card.art(0)
                                sourceSize.width: 160
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }

                        Rectangle {
                            width: parent.width * 0.75
                            height: 8
                            radius: 2
                            color: card.neutrals.text
                        }

                        Rectangle {
                            width: parent.width * 0.5
                            height: 6
                            radius: 2
                            color: card.neutrals.textMuted
                        }

                        Rectangle {
                            width: parent.width
                            height: 20
                            radius: 4
                            color: card.accents.accent
                        }
                    }
                }

                Item {
                    id: tileArea
                    readonly property real tile: (height - grid.spacing) / 2
                    anchors.top: bar.bottom
                    anchors.left: side.right
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.space2

                    Grid {
                        id: grid
                        spacing: Theme.space2
                        columns: Math.max(1, Math.floor((tileArea.width + spacing) / (tileArea.tile + spacing)))

                    Repeater {
                        model: 2 * grid.columns

                        Rectangle {
                            required property int index
                            width: (tileArea.width - (grid.columns - 1) * grid.spacing) / grid.columns
                            height: tileArea.tile
                            radius: 4
                            color: card.neutrals.surface
                            border.color: index === 0 ? card.accents.accent : card.neutrals.separator
                            border.width: index === 0 ? 2 : 1

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 3
                                radius: 2
                                color: card.neutrals.surfaceMuted
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: card.art(parent.parent.index)
                                    sourceSize.width: 128
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                            }
                        }
                    }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMedium
                color: "transparent"
                border.color: card.chosen ? Theme.accent : Theme.separator
                border.width: card.chosen ? 2 : 1
            }

            FocusRing {
                shown: card.activeFocus
                cornerRadius: Theme.radiusMedium
            }
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.space2

            MenuIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: card.chosen
                path: Theme.iconCheck
                color: Theme.accent
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: card.text
                font.pixelSize: Theme.fontBody
                font.bold: card.chosen
                color: card.chosen ? Theme.accent : Theme.text
            }
        }
    }
}
