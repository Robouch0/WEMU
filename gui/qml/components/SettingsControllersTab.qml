import QtQuick
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    // The Wii U button waiting for a controller button; empty when not capturing.
    property string listeningFor: ""

    function selectProfile(profile) {
        listeningFor = ""
        InputProfileManager.selectProfile(profile)
    }

    // Replacing the binding list resets the view, so put the cursor back on the row just changed.
    function finishCapture(button) {
        const row = bindingList.currentIndex
        InputProfileManager.setBinding(listeningFor, button)
        listeningFor = ""
        bindingList.currentIndex = row
    }

    function startCapture(row) {
        bindingList.currentIndex = row
        listeningFor = InputProfileManager.bindingModel[row].wiiu
    }

    // Keeps a hand-placed preview intact: the whole drawing scales, so no button moves.
    component ScaledPreview: Item {
        id: frame
        default property Item preview
        readonly property real fit: preview ? Math.min(1, width / preview.width) : 1
        // The drawings run past the size they declare, so measure what they actually cover.
        implicitHeight: preview ? preview.childrenRect.height * fit : 0
        onPreviewChanged: {
            preview.parent = frame
            preview.transformOrigin = Item.TopLeft
        }
        Binding {
            target: frame.preview
            property: "scale"
            value: frame.fit
        }
        Binding {
            target: frame.preview
            property: "y"
            value: Math.max(0, (frame.height - frame.implicitHeight) / 2)
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width >= 880 ? 3 : 1
        columnSpacing: Theme.space4
        rowSpacing: Theme.space4

        SettingsCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 3
            Layout.fillHeight: true
            title: "Physical controller"
            ScaledPreview {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ControllerPreview {}
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 4
            Layout.fillHeight: true
            title: "Mapping"

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2

                MenuButton {
                    id: profile1
                    Layout.fillWidth: true
                    text: "Profile 1"
                    primary: InputProfileManager.currentProfileIndex === 0
                    KeyNavigation.right: profile2
                    KeyNavigation.down: bindingList
                    onClicked: tab.selectProfile(0)
                }
                MenuButton {
                    id: profile2
                    Layout.fillWidth: true
                    text: "Profile 2"
                    primary: InputProfileManager.currentProfileIndex === 1
                    KeyNavigation.left: profile1
                    KeyNavigation.right: profile3
                    KeyNavigation.down: bindingList
                    onClicked: tab.selectProfile(1)
                }
                MenuButton {
                    id: profile3
                    Layout.fillWidth: true
                    text: "Profile 3"
                    primary: InputProfileManager.currentProfileIndex === 2
                    KeyNavigation.left: profile2
                    KeyNavigation.down: bindingList
                    onClicked: tab.selectProfile(2)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space3
                Layout.rightMargin: Theme.space3
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: "Controller"
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    color: Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: "Wii U"
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    color: Theme.textMuted
                }
            }

            ListView {
                id: bindingList
                Layout.fillWidth: true
                implicitHeight: contentHeight
                interactive: false
                activeFocusOnTab: true
                keyNavigationEnabled: false
                model: InputProfileManager.bindingModel
                property bool confirmPressed: false

                onCurrentIndexChanged: tab.ensureVisible(currentItem)

                Keys.onUpPressed: {
                    if (tab.listeningFor !== "") return
                    if (currentIndex > 0) currentIndex--
                    else [profile1, profile2, profile3][InputProfileManager.currentProfileIndex].forceActiveFocus()
                }
                Keys.onDownPressed: {
                    if (tab.listeningFor !== "") return
                    if (currentIndex < count - 1) currentIndex++
                    else resetButton.forceActiveFocus()
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        confirmPressed = !event.isAutoRepeat
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape && tab.listeningFor !== "") {
                        tab.listeningFor = ""
                        event.accepted = true
                    }
                }
                // Capture opens on release so the press of A is not itself taken as the new binding.
                Keys.onReleased: event => {
                    if (!confirmPressed || event.isAutoRepeat) return
                    confirmPressed = false
                    tab.startCapture(currentIndex)
                    event.accepted = true
                }

                delegate: BindingRow {
                    width: bindingList.width
                    wiiuLabel: modelData.wiiuLabel
                    xboxButton: modelData.xboxButton
                    isListening: tab.listeningFor === modelData.wiiu
                    focused: bindingList.activeFocus && ListView.isCurrentItem
                    onRowClicked: {
                        bindingList.forceActiveFocus()
                        tab.startCapture(index)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3

                Text {
                    Layout.fillWidth: true
                    text: tab.listeningFor !== "" ? "Press a controller button. Esc or waiting 5 seconds cancels."
                                                  : InputManager.connectedDevices().length > 0
                                                    ? "Connected: " + InputManager.connectedDevices().join(", ")
                                                    : "No controller connected"
                    font.pixelSize: Theme.fontCaption
                    font.bold: tab.listeningFor !== ""
                    color: tab.listeningFor !== "" ? Theme.warning : Theme.textMuted
                    wrapMode: Text.WordWrap
                }

                MenuButton {
                    id: resetButton
                    text: "Reset to default"
                    destructive: true
                    onClicked: {
                        tab.listeningFor = ""
                        InputProfileManager.resetCurrentProfile()
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 3
            Layout.fillHeight: true
            title: "Virtual GamePad"
            ScaledPreview {
                Layout.fillWidth: true
                Layout.fillHeight: true
                GamepadPreview {}
            }
        }
    }
}
