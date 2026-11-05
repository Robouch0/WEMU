import QtQuick
import QtQuick.Shapes

// A line icon drawn from an SVG path on a 24 by 24 grid.
Shape {
    id: root

    property string path: ""
    property color color: "black"

    readonly property real size: 20

    width: size
    height: size
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: root.color
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        scale: Qt.size(root.size / 24, root.size / 24)
        PathSvg { path: root.path }
    }
}
