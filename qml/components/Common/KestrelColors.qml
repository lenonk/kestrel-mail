pragma Singleton
import QtQuick

QtObject {
    // Links and interactive text
    readonly property color linkBlue: "#4ea3ff"

    // Status / error
    readonly property color errorRed: "#bf3354"

    // Tag defaults
    readonly property color tagDefaultAccent: "#D6E8FF"
    readonly property color tagDarkText: "#1E3C5A"
    readonly property color tagDarkOnBright: "#1d2433"
    readonly property color tagLightOnDark: "#eef3ff"
    readonly property color importantYellow: "#FFD600"

    // Calendar events
    readonly property color calendarEventDefault: "#9a8cff"
    readonly property color calendarDarkText: "#111111"
    readonly property color calendarLightText: "#ffffff"
    readonly property color calendarTodayFallback: "#2979ff"

    // Utility buttons
    readonly property color utilityHighlight: "#3daee9"

    // Search
    readonly property color searchOrange: "#E67E22"
}
