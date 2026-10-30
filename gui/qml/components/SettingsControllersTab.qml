import QtQuick
import QtQuick.Layouts
import gui 1.0

SettingsTabPage {
    id: tab

    contentMargin: Theme.space4

    // The Wii U button waiting for a controller button; empty when not capturing.
    property string listeningFor: ""
    // Test mode gives every input to the previews, so none of it moves through the menus.
    property bool testing: false
    property real stopProgress: 0
    readonly property bool holdsInput: listeningFor !== "" || testing
    property var controllers: InputManager.controllerNames()

    function cancelInputModes() {
        listeningFor = ""
        testing = false
    }

    onTestingChanged: stopHold.stop()

    Connections {
        target: InputManager
        function onDevicesChanged() { tab.controllers = InputManager.controllerNames() }
        function onButtonChanged(button, pressed, device) {
            if (!tab.testing || button !== "B") return
            if (pressed) stopHold.restart()
            else stopHold.stop()
        }
    }

    SequentialAnimation {
        id: stopHold
        onStopped: tab.stopProgress = 0
        NumberAnimation { target: tab; property: "stopProgress"; from: 0; to: 1; duration: 1200 }
        ScriptAction { script: tab.testing = false }
    }

    readonly property int activeProfile: InputProfileManager.currentProfileIndex
    readonly property int viewedProfile: InputProfileManager.editedProfileIndex
    readonly property Item viewedProfileButton: [profile1, profile2, profile3][viewedProfile]
    readonly property Item belowProfiles: useButton.visible ? useButton : bindingList

    Component.onCompleted: InputProfileManager.editProfile(activeProfile)

    function openProfile(profile) {
        listeningFor = ""
        InputProfileManager.editProfile(profile)
        if (useButton.visible) useButton.forceActiveFocus()
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
        // Both drawings share one scale and box, so their fronts and backs line up across the page.
        // nudge is in drawing pixels and centers this drawing's art on the shared rows.
        property real nudge: 0
        readonly property real naturalHeight: 520
        readonly property real artWidth: 345
        readonly property real tallestFit: (tab.height - 2 * tab.contentMargin - 100) / naturalHeight
        readonly property real fit: Math.min(1, width / artWidth, stacked.columns > 1 ? tallestFit : 1)
        // The drawings run past the size they declare, so measure what they actually cover.
        implicitHeight: naturalHeight * fit
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
            value: Math.max(0, (frame.height - frame.implicitHeight) / 2) + frame.nudge * frame.fit
        }
        Binding {
            target: frame.preview
            property: "x"
            value: frame.preview ? (frame.width - frame.preview.width * frame.fit) / 2 : 0
        }
    }

    GridLayout {
        id: stacked
        Layout.fillWidth: true
        columns: width >= 880 ? 3 : 1
        columnSpacing: Theme.space4
        rowSpacing: Theme.space4

        SettingsCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 7
            Layout.fillHeight: true
            highlighted: tab.testing

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3

                Text {
                    Layout.fillWidth: true
                    text: tab.controllers.length > 0 ? tab.controllers.join(", ") : qsTr("No controller connected")
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                }

            MenuButton {
                id: testButton
                primary: tab.testing
                soft: !tab.testing
                iconPath: Theme.iconTest
                text: tab.testing ? qsTr("Exit test") : qsTr("Test")
                Keys.onEscapePressed: event => {
                    if (tab.testing) tab.testing = false
                    else event.accepted = false
                }
                onClicked: tab.testing = !tab.testing
            }
            }

            ScaledPreview {
                Layout.fillWidth: true
                Layout.fillHeight: true
                nudge: -11
                ControllerPreview {}
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 6
            Layout.fillHeight: true
            id: mappingCard
            visible: !tab.testing
            title: qsTr("Button mapping")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2

                MenuButton {
                    id: profile1
                    Layout.fillWidth: true
                    leftPadding: Theme.space1
                    rightPadding: Theme.space1
                    text: qsTr("Profile %1").arg(1)
                    soft: tab.viewedProfile === 0
                    font.bold: soft
                    iconPath: tab.activeProfile === 0 ? Theme.iconCheck : ""
                    KeyNavigation.up: testButton
                    KeyNavigation.right: profile2
                    KeyNavigation.down: tab.belowProfiles
                    onActiveFocusChanged: if (activeFocus) InputProfileManager.editProfile(0)
                    onClicked: tab.openProfile(0)
                }
                MenuButton {
                    id: profile2
                    Layout.fillWidth: true
                    leftPadding: Theme.space1
                    rightPadding: Theme.space1
                    text: qsTr("Profile %1").arg(2)
                    soft: tab.viewedProfile === 1
                    font.bold: soft
                    iconPath: tab.activeProfile === 1 ? Theme.iconCheck : ""
                    KeyNavigation.up: testButton
                    KeyNavigation.left: profile1
                    KeyNavigation.right: profile3
                    KeyNavigation.down: tab.belowProfiles
                    onActiveFocusChanged: if (activeFocus) InputProfileManager.editProfile(1)
                    onClicked: tab.openProfile(1)
                }
                MenuButton {
                    id: profile3
                    Layout.fillWidth: true
                    leftPadding: Theme.space1
                    rightPadding: Theme.space1
                    text: qsTr("Profile %1").arg(3)
                    soft: tab.viewedProfile === 2
                    font.bold: soft
                    iconPath: tab.activeProfile === 2 ? Theme.iconCheck : ""
                    KeyNavigation.up: testButton
                    KeyNavigation.left: profile2
                    KeyNavigation.down: tab.belowProfiles
                    onActiveFocusChanged: if (activeFocus) InputProfileManager.editProfile(2)
                    onClicked: tab.openProfile(2)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                spacing: Theme.space3

                Text {
                    Layout.fillWidth: true
                    text: useButton.visible ? qsTr("Games use Profile %1").arg(tab.activeProfile + 1) : qsTr("Games use this profile")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    elide: Text.ElideRight
                }

                MenuButton {
                    id: useButton
                    implicitHeight: 36
                    primary: true
                    visible: tab.viewedProfile !== tab.activeProfile
                    text: qsTr("Use for games")
                    KeyNavigation.up: tab.viewedProfileButton
                    KeyNavigation.down: bindingList
                    onClicked: {
                        InputProfileManager.selectProfile(tab.viewedProfile)
                        tab.viewedProfileButton.forceActiveFocus()
                    }
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
                    text: qsTr("Controller")
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
                    else if (useButton.visible) useButton.forceActiveFocus()
                    else tab.viewedProfileButton.forceActiveFocus()
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
                    visible: tab.listeningFor !== ""
                    text: qsTr("Esc cancels")
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    color: Theme.warning
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                    visible: tab.listeningFor === ""
                }

                MenuButton {
                    id: resetButton
                    text: qsTr("Reset to default")
                    destructive: true
                    onClicked: {
                        tab.listeningFor = ""
                        InputProfileManager.resetEditedProfile()
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        SettingsLiveInput {
            Layout.fillWidth: true
            Layout.preferredWidth: 6
            Layout.fillHeight: true
            // Matches the Mapping card it replaces, so entering test mode moves nothing.
            Layout.minimumHeight: mappingCard.implicitHeight
            visible: tab.testing
            exitProgress: tab.stopProgress
        }

        SettingsCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 7
            Layout.fillHeight: true
            title: qsTr("Virtual GamePad")
            highlighted: tab.testing
            ScaledPreview {
                Layout.fillWidth: true
                Layout.fillHeight: true
                nudge: 11
                GamepadPreview {}
            }
        }
    }
}
