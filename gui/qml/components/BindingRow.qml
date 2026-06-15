import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property string wiiuLabel: ""
    property string xboxButton: ""
    property bool isListening: false

    signal rowClicked()

    height: 36
    color: root.isListening
        ? "#fff5e0"
        : (mouseArea.containsMouse ? "#ececf0" : "transparent")
    radius: 4
    border.color: root.isListening ? "#f5a623" : "transparent"
    border.width: root.isListening ? 1 : 0

    Behavior on color { ColorAnimation { duration: 120 } }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.rowClicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.isListening ? "..." : (root.xboxButton !== "" ? root.xboxButton : "—")
                color: root.isListening ? "#c46a00" : "#1a1a1a"
                font.pixelSize: 13
                font.italic: root.isListening
            }
        }

        Rectangle {
            width: 1
            Layout.fillHeight: true
            color: "#d0d0d5"
            Layout.topMargin: 6
            Layout.bottomMargin: 6
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 15
                text: root.wiiuLabel
                color: "#555555"
                font.pixelSize: 13
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: "#e0e0e5"
    }
}
