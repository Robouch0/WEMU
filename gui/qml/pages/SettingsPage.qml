import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import gui 1.0

Rectangle {
    id: page
    color: Theme.bg
    anchors.fill: parent

    readonly property var tabs: [qsTr("General"), qsTr("Audio"), qsTr("Controllers"), qsTr("Mobile GamePad")]
    readonly property string listeningFor: controllersTab.listeningFor

    function goHome() {
        rootWindow.settingsTab = 0
        mainLoader.source = "pages/MainMenu.qml"
    }

    function enterTab() { tabStack.children[rail.currentIndex].enter() }

    function changeTab(step) {
        const next = rail.currentIndex + step
        if (next < 0 || next >= tabs.length) return
        controllersTab.cancelInputModes()
        rail.currentIndex = next
        rail.forceActiveFocus()
    }

    Component.onCompleted: rail.forceActiveFocus()

    OpacityAnimator on opacity { from: 0; to: 1; duration: Theme.durationNormal }

    // Page Up and Page Down arrive from LB and RB, wherever focus is inside the page.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp) changeTab(-1)
        else if (event.key === Qt.Key_PageDown) changeTab(1)
        else return
        event.accepted = true
    }

    Binding {
        target: MenuKeys
        property: "suspended"
        value: controllersTab.holdsInput
    }

    Timer {
        interval: 5000
        running: page.listeningFor !== ""
        onTriggered: controllersTab.listeningFor = ""
    }

    Connections {
        target: InputManager

        function onButtonChanged(button, pressed, device) {
            if (!pressed || page.listeningFor === "") return
            if (InputProfileManager.isKnownXboxButton(button)) controllersTab.finishCapture(button)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight + 2 * Theme.space3
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
                anchors.leftMargin: Theme.space4
                anchors.rightMargin: Theme.space5
                spacing: Theme.space4

                MenuButton {
                    text: qsTr("← Library")
                    focusPolicy: Qt.NoFocus
                    onClicked: page.goHome()
                }

                ListView {
                    id: rail
                    Layout.preferredWidth: contentWidth
                    Layout.preferredHeight: Theme.controlHeight
                    orientation: ListView.Horizontal
                    spacing: Theme.space1
                    interactive: false
                    currentIndex: rootWindow.settingsTab
                    keyNavigationEnabled: true
                    model: page.tabs
                    highlightMoveDuration: Theme.durationNormal
                    highlightMoveVelocity: -1
                    highlightResizeDuration: Theme.durationNormal
                    highlightResizeVelocity: -1
                    onCurrentIndexChanged: tabEnter.restart()

                    highlight: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.accentSoft

                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 2 * Theme.space3
                            height: 3
                            radius: 2
                            color: Theme.accent
                        }
                    }

                    Keys.onDownPressed: page.enterTab()
                    Keys.onSpacePressed: page.enterTab()
                    Keys.onReturnPressed: page.enterTab()
                    Keys.onEnterPressed: page.enterTab()
                    Keys.onEscapePressed: page.goHome()

                    delegate: Rectangle {
                        id: railItem
                        required property string modelData
                        required property int index
                        readonly property bool selected: ListView.isCurrentItem

                        width: railText.implicitWidth + 2 * Theme.space4
                        height: rail.height
                        radius: Theme.radiusSmall
                        color: !selected && railMouse.containsMouse ? Theme.surfaceMuted : "transparent"

                        Text {
                            id: railText
                            anchors.centerIn: parent
                            text: railItem.modelData
                            font.pixelSize: Theme.fontBody
                            font.bold: railItem.selected
                            color: railItem.selected ? Theme.accent : Theme.text
                        }

                        FocusRing {
                            anchors.margins: -Theme.focusRingWidth
                            shown: railItem.selected && rail.activeFocus
                        }

                        MouseArea {
                            id: railMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                controllersTab.cancelInputModes()
                                rail.currentIndex = railItem.index
                                rail.forceActiveFocus()
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: controllersTab.testing ? qsTr("Hold B to exit test mode")
                        : page.listeningFor !== "" ? qsTr("Waiting for a button")
                        : rail.activeFocus ? qsTr("Down open · LB / RB tabs · B library")
                        : qsTr("A select · LB / RB tabs · B back")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }
        }

        StackLayout {
            id: tabStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: rail.currentIndex
            transform: Translate { id: tabShift }

            ParallelAnimation {
                id: tabEnter
                NumberAnimation { target: tabStack; property: "opacity"; from: 0.4; to: 1; duration: Theme.durationNormal }
                NumberAnimation { target: tabShift; property: "y"; from: Theme.space3; to: 0; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
            }

            SettingsGeneralTab { onLeaveRequested: rail.forceActiveFocus() }
            SettingsAudioTab { onLeaveRequested: rail.forceActiveFocus() }
            SettingsControllersTab {
                id: controllersTab
                onLeaveRequested: rail.forceActiveFocus()
            }
            RemoteGamepadPanel { onLeaveRequested: rail.forceActiveFocus() }
        }
    }
}
