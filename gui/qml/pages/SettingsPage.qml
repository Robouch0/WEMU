import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import gui 1.0

Rectangle {
    id: page
    color: Theme.bg
    anchors.fill: parent

    readonly property var tabs: ["General", "Controllers", "Mobile GamePad"]
    readonly property string listeningFor: controllersTab.listeningFor

    function goHome() {
        rootWindow.settingsTab = 0
        mainLoader.source = "pages/MainMenu.qml"
    }

    function enterTab() { tabStack.children[rail.currentIndex].enter() }

    function changeTab(step) {
        const next = rail.currentIndex + step
        if (next < 0 || next >= tabs.length) return
        controllersTab.listeningFor = ""
        rail.currentIndex = next
        rail.forceActiveFocus()
    }

    Component.onCompleted: rail.forceActiveFocus()

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
        value: page.listeningFor !== ""
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

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 240
            color: Theme.surface

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.separator
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space4
                spacing: Theme.space4

                MenuButton {
                    Layout.fillWidth: true
                    text: "← Library"
                    focusPolicy: Qt.NoFocus
                    onClicked: page.goHome()
                }

                Text {
                    Layout.leftMargin: Theme.space2
                    text: "Settings"
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                    color: Theme.text
                }

                ListView {
                    id: rail
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.space2
                    interactive: false
                    currentIndex: rootWindow.settingsTab
                    keyNavigationEnabled: true
                    model: page.tabs

                    Keys.onRightPressed: page.enterTab()
                    Keys.onSpacePressed: page.enterTab()
                    Keys.onReturnPressed: page.enterTab()
                    Keys.onEnterPressed: page.enterTab()
                    Keys.onEscapePressed: page.goHome()

                    delegate: Rectangle {
                        id: railItem
                        required property string modelData
                        required property int index
                        readonly property bool selected: ListView.isCurrentItem

                        width: rail.width
                        height: Theme.controlHeight + Theme.space1
                        radius: Theme.radiusSmall
                        color: selected ? Theme.accentSoft
                                        : railMouse.containsMouse ? Theme.surfaceMuted : "transparent"

                        Rectangle {
                            visible: railItem.selected
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 4
                            height: parent.height - 2 * Theme.space3
                            radius: 2
                            color: Theme.accent
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space4
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: railItem.modelData
                            font.pixelSize: Theme.fontBody
                            font.bold: railItem.selected
                            color: railItem.selected ? Theme.accent : Theme.text
                            elide: Text.ElideRight
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
                                controllersTab.listeningFor = ""
                                rail.currentIndex = railItem.index
                                rail.forceActiveFocus()
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "LB / RB switch tabs · B back"
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
            }
        }

        StackLayout {
            id: tabStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: rail.currentIndex

            SettingsGeneralTab { onLeaveRequested: rail.forceActiveFocus() }
            SettingsControllersTab {
                id: controllersTab
                onLeaveRequested: rail.forceActiveFocus()
            }
            RemoteGamepadPanel { onLeaveRequested: rail.forceActiveFocus() }
        }
    }
}
