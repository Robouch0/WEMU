import QtQuick
import QtQuick.Controls

Rectangle {
    color: "#0a0a0f"
    anchors.fill: parent

    Button {
        text: "Settings ▸"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        onClicked: mainLoader.source = "pages/SettingsPage.qml"
    }

    ListView {
        id: carousel
        anchors.centerIn: parent
        width: parent.width
        height: 220
        orientation: ListView.Horizontal
        snapMode: ListView.SnapToItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: width / 2 - 110
        preferredHighlightEnd: width / 2 + 110
        spacing: 24
        clip: true

        model: TitleScanner

        delegate: Rectangle {
            id: delegateRoot
            property string gameName:      model.name
            property string gamePublisher: model.publisher
            property string gameVersion:   model.version
            property string gameRpxPath:   model.rpxPath
            property bool   isCurrent:     ListView.isCurrentItem

            width: 200
            height: 200
            radius: 12
            color: ListView.isCurrentItem ? "#1e1e2e" : "#13131a"
            border.color: ListView.isCurrentItem ? "#7c6af7" : "#2a2a3a"
            border.width: ListView.isCurrentItem ? 2 : 1

            Behavior on color { ColorAnimation { duration: 150 } }
            Behavior on border.color { ColorAnimation { duration: 150 } }

            MouseArea {
                anchors.fill: parent
                onClicked: carousel.currentIndex = index
            }

            Column {
                anchors.centerIn: parent
                spacing: 10
                width: parent.width - 24

                Text {
                    width: parent.width
                    text: model.name !== "" ? model.name : "Unknown"
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: model.publisher !== "" ? model.publisher : ""
                    color: "#888899"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Button {
                    visible: delegateRoot.isCurrent
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "▶  Launch"
                    onClicked: EmulatorLauncher.launch(delegateRoot.gameRpxPath)
                }
            }
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: carousel.bottom
        anchors.topMargin: 20
        spacing: 6

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: carousel.currentItem ? carousel.currentItem.gameName : "No games found"
            color: "white"
            font.pixelSize: 22
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: carousel.currentItem ? carousel.currentItem.gamePublisher : ""
            color: "#888899"
            font.pixelSize: 14
        }
    }
    Connections {
        target: EmulatorLauncher
        function onStateChanged(running) {
            rootWindow.visible = !running
        }
    }
}
