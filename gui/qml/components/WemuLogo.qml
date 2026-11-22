import QtQuick
import QtQuick.Shapes

// The colored WEMU letters of the intro, drawn as thick strokes so they stay sharp at any size.
// Set height (or width); the other side follows the letters' proportions.
Item {
    id: logo

    readonly property real designWidth: 798
    readonly property real designHeight: 175
    readonly property real stroke: 36

    implicitHeight: 28
    implicitWidth: height * designWidth / designHeight
    width: implicitWidth

    component Letter: ShapePath {
        strokeWidth: logo.stroke
        fillColor: "transparent"
        capStyle: ShapePath.FlatCap
        joinStyle: ShapePath.RoundJoin
    }

    Item {
        // The W's tops are cut level: its strokes run past the top edge and are clipped there.
        width: logo.designWidth
        height: logo.designHeight
        clip: true
        scale: logo.height / logo.designHeight
        transformOrigin: Item.TopLeft

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            Letter {
                strokeColor: "#50c440"
                PathPolyline { path: [Qt.point(10, -30), Qt.point(71, 157), Qt.point(119, 13), Qt.point(167, 157), Qt.point(228, -30)] }
            }
            Letter {
                strokeColor: "#ee7840"
                PathPolyline { path: [Qt.point(391, 18), Qt.point(288, 18), Qt.point(288, 157), Qt.point(391, 157)] }
            }
            Letter {
                strokeColor: "#ee7840"
                PathPolyline { path: [Qt.point(288, 87.5), Qt.point(377, 87.5)] }
            }
            Letter {
                strokeColor: "#b44c48"
                PathPolyline { path: [Qt.point(441, 175), Qt.point(441, 18), Qt.point(525, 111), Qt.point(607, 18), Qt.point(607, 175)] }
            }
            Letter {
                strokeColor: "#4c68f8"
                startX: 676; startY: 0
                PathLine { x: 676; y: 105 }
                PathArc { x: 780; y: 105; radiusX: 52; radiusY: 52; direction: PathArc.Counterclockwise }
                PathLine { x: 780; y: 0 }
            }
        }
    }
}
