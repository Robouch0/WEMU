pragma Singleton
import QtQuick

// Design tokens. Components use these names only, so a theme can re-map them in one place.
QtObject {
    // Color
    readonly property bool dark: AppPreferences.themeMode === "dark"
    readonly property var neutrals: dark ? darkNeutrals : lightNeutrals
    readonly property var accents: (dark ? darkAccents : lightAccents)[AppPreferences.accentColor]

    readonly property var lightNeutrals: ({
        bg: "#EEF0F4", surface: "#FFFFFF", surfaceMuted: "#E3E6EC", surfaceHover: "#D5D9E1", separator: "#D2D7E0",
        text: "#1A1D24", textMuted: "#555D6B", borderControl: "#7C8698",
        success: "#157A46", warning: "#8A5A00", danger: "#B3261E", previewStage: "transparent"
    })
    readonly property var darkNeutrals: ({
        bg: "#13161B", surface: "#1D2128", surfaceMuted: "#2A2F38", surfaceHover: "#363C47", separator: "#333945",
        text: "#ECEFF4", textMuted: "#AEB6C4", borderControl: "#818B9C",
        success: "#62D394", warning: "#E9BC63", danger: "#FF9A90", previewStage: "#434B5C"
    })
    readonly property var lightAccents: ({
        blue: { accent: "#0B63CE", hover: "#0A56B3", soft: "#E4EEFB", softHover: "#D3E3F8", onAccent: "#FFFFFF" },
        teal: { accent: "#0F7173", hover: "#0C5F60", soft: "#DFF1F1", softHover: "#C9E6E6", onAccent: "#FFFFFF" },
        green: { accent: "#1B6E3F", hover: "#155F35", soft: "#E2F2E8", softHover: "#CDE7D7", onAccent: "#FFFFFF" },
        amber: { accent: "#8F5B00", hover: "#7A4E00", soft: "#FBEFD9", softHover: "#F6E3C0", onAccent: "#FFFFFF" },
        orange: { accent: "#B0460C", hover: "#993C0A", soft: "#FCE9DE", softHover: "#F9D9C6", onAccent: "#FFFFFF" },
        red: { accent: "#BF2A2A", hover: "#A62323", soft: "#FBE6E6", softHover: "#F7D4D4", onAccent: "#FFFFFF" },
        pink: { accent: "#B8245F", hover: "#A01E52", soft: "#FBE4EE", softHover: "#F7D2E2", onAccent: "#FFFFFF" },
        purple: { accent: "#6B3FC9", hover: "#5D35B0", soft: "#ECE6FA", softHover: "#DFD5F6", onAccent: "#FFFFFF" }
    })
    readonly property var darkAccents: ({
        blue: { accent: "#74B4FF", hover: "#94C6FF", soft: "#1F3550", softHover: "#28435F", onAccent: "#0E1A2B" },
        teal: { accent: "#4FD0CC", hover: "#74DDD9", soft: "#15393A", softHover: "#1D4849", onAccent: "#082020" },
        green: { accent: "#62D394", hover: "#82E0AB", soft: "#1B3A2A", softHover: "#254A36", onAccent: "#0C2015" },
        amber: { accent: "#F2C14E", hover: "#F6D27C", soft: "#3E3116", softHover: "#4E3E1C", onAccent: "#251A03" },
        orange: { accent: "#FFA26B", hover: "#FFB98D", soft: "#47291A", softHover: "#583321", onAccent: "#2B1407" },
        red: { accent: "#FF8F8F", hover: "#FFABAB", soft: "#482326", softHover: "#5A2C30", onAccent: "#2B0F10" },
        pink: { accent: "#FF8FBC", hover: "#FFABCD", soft: "#472337", softHover: "#592C45", onAccent: "#2C0E1E" },
        purple: { accent: "#B79CFF", hover: "#CBB6FF", soft: "#2F2752", softHover: "#3B3166", onAccent: "#1A1236" }
    })

    readonly property var accentNames: ["blue", "teal", "green", "amber", "orange", "red", "pink", "purple"]

    function accentFor(name) { return (dark ? darkAccents : lightAccents)[name].accent }
    function onAccentFor(name) { return (dark ? darkAccents : lightAccents)[name].onAccent }

    readonly property color bg: neutrals.bg
    readonly property color surface: neutrals.surface
    readonly property color surfaceMuted: neutrals.surfaceMuted
    readonly property color surfaceHover: neutrals.surfaceHover
    readonly property color separator: neutrals.separator
    readonly property color text: neutrals.text
    readonly property color textMuted: neutrals.textMuted
    readonly property color borderControl: neutrals.borderControl
    readonly property color accent: accents.accent
    readonly property color accentHover: accents.hover
    readonly property color accentSoft: accents.soft
    readonly property color accentSoftHover: accents.softHover
    readonly property color textOnAccent: accents.onAccent
    readonly property color success: neutrals.success
    readonly property color warning: neutrals.warning
    readonly property color danger: neutrals.danger
    // The controller drawings are black, so they need a lighter backdrop on dark cards.
    readonly property color previewStage: neutrals.previewStage
    // Pressed buttons sit on that black artwork in every theme.
    readonly property color highlightOnArt: darkAccents[AppPreferences.accentColor].accent

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

    // Icons, as SVG paths on a 24 by 24 grid
    readonly property string iconFolder: "M3 7 a2 2 0 0 1 2 -2 h4 l2 3 h8 a2 2 0 0 1 2 2 v8 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 Z"
    readonly property string iconSettings: "M4 7 h9 M19 7 h1 M4 17 h1 M11 17 h9 M16 4.5 a2.5 2.5 0 1 0 0.01 0 Z M8 14.5 a2.5 2.5 0 1 0 0.01 0 Z"
    readonly property string iconPower: "M12 3 v8 M6.5 7 a8 8 0 1 0 11 0"
    readonly property string iconCheck: "M5 12.5 l4.5 4.5 L19 7"
    readonly property string iconPhone: "M9 3 h6 a2 2 0 0 1 2 2 v14 a2 2 0 0 1 -2 2 h-6 a2 2 0 0 1 -2 -2 V5 a2 2 0 0 1 2 -2 Z M11 18 h2"
    readonly property string iconTest: "M7 8 h10 a4 4 0 0 1 4 4 v3 a2.5 2.5 0 0 1 -4.6 1.3 L15 14 H9 l-1.4 2.3 A2.5 2.5 0 0 1 3 15 v-3 a4 4 0 0 1 4 -4 Z M8 10.5 v3 M6.5 12 h3 M16 12 h0.01"

    // In-game session overlay, which stays dark over the game image.
    readonly property color sessionPanel: "#202632"
    readonly property color sessionPanelText: "#f2f5fa"
    readonly property color sessionMutedText: "#b1bdce"
    readonly property color sessionAccent: lightAccents[AppPreferences.accentColor].accent
    readonly property int sessionRadius: 10
}
