pragma Singleton
import QtQuick

// Design tokens. Components use these names only, so a theme can re-map them in one place.
QtObject {
    // Color
    property color bg: "#EEF0F4"
    property color surface: "#FFFFFF"
    property color surfaceMuted: "#E3E6EC"
    property color surfaceHover: "#D5D9E1"
    property color separator: "#D2D7E0"
    property color text: "#1A1D24"
    property color textMuted: "#555D6B"
    property color borderControl: "#7C8698"
    property color accent: "#0B63CE"
    property color accentHover: "#0A56B3"
    property color accentSoft: "#E4EEFB"
    property color textOnAccent: "#FFFFFF"
    property color success: "#157A46"
    property color warning: "#8A5A00"
    property color danger: "#B3261E"

    // Spacing
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32
    readonly property int space7: 48

    // Type
    readonly property int fontCaption: 14
    readonly property int fontBody: 16
    readonly property int fontSubtitle: 20
    readonly property int fontTitle: 28
    readonly property int fontDisplay: 36

    // Shape
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 16
    readonly property int controlHeight: 44
    readonly property int focusRingWidth: 3
    readonly property int focusRingOffset: 3

    // Motion
    readonly property int durationFast: 120
    readonly property int durationNormal: 200

    // In-game session overlay, which stays dark over the game image.
    readonly property color sessionPanel: "#202632"
    readonly property color sessionPanelText: "#f2f5fa"
    readonly property color sessionMutedText: "#b1bdce"
    readonly property color sessionAccent: "#3498ff"
    readonly property int sessionRadius: 10
}
