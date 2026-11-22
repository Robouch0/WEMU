import QtQuick
import Wemu 1.0
import gui 1.0

// A dotted Earth that turns to a place, or spins slowly when no place is chosen.
Item {
    id: globe

    property real centerLatitude: 15
    property real centerLongitude: 0
    property real markerLatitude: 0
    property real markerLongitude: 0
    property bool hasMarker: false

    readonly property real diameter: Math.min(width, height) - 4
    // The marker's position on the visible face, where a positive z faces the viewer.
    readonly property var marker: {
        const r = Math.PI / 180
        const delta = (markerLongitude - centerLongitude) * r
        const sinCenter = Math.sin(centerLatitude * r), cosCenter = Math.cos(centerLatitude * r)
        const sinPoint = Math.sin(markerLatitude * r), cosPoint = Math.cos(markerLatitude * r)
        return {
            x: cosPoint * Math.sin(delta),
            y: cosCenter * sinPoint - sinCenter * cosPoint * Math.cos(delta),
            z: sinCenter * sinPoint + cosCenter * cosPoint * Math.cos(delta)
        }
    }

    function showPlace(latitude, longitude) {
        spin.stop()
        markerLatitude = latitude
        markerLongitude = longitude
        hasMarker = true
        const shortestTurn = ((longitude - centerLongitude) % 360 + 540) % 360 - 180
        turnLatitude.to = latitude
        turnLongitude.to = centerLongitude + shortestTurn
        turn.restart()
    }

    function showWholeWorld() {
        hasMarker = false
        turn.stop()
        spin.from = centerLongitude
        spin.to = centerLongitude + 360
        spin.restart()
    }

    ParallelAnimation {
        id: turn
        NumberAnimation { id: turnLatitude; target: globe; property: "centerLatitude"; duration: 700; easing.type: Easing.InOutCubic }
        NumberAnimation { id: turnLongitude; target: globe; property: "centerLongitude"; duration: 700; easing.type: Easing.InOutCubic }
    }

    NumberAnimation {
        id: spin
        target: globe
        property: "centerLongitude"
        duration: 60000
        loops: Animation.Infinite
        paused: running && !globe.visible
    }

    Rectangle {
        anchors.centerIn: parent
        width: globe.diameter
        height: width
        radius: width / 2
        color: Theme.accentSoft
    }

    GlobeDots {
        anchors.centerIn: parent
        width: globe.diameter
        height: width
        centerLatitude: globe.centerLatitude
        centerLongitude: globe.centerLongitude
        color: Theme.borderControl
    }

    Item {
        visible: globe.hasMarker && globe.marker.z > 0
        x: globe.width / 2 + globe.diameter / 2 * globe.marker.x
        y: globe.height / 2 - globe.diameter / 2 * globe.marker.y

        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: Theme.accent
        }

        Rectangle {
            anchors.centerIn: parent
            width: 26
            height: 26
            radius: 13
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
        }
    }
}
