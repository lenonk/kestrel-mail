import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import "." as Calendar

Item {
    id: root

    property int dayCount: 7
    property int startHour: 0
    property int endHour: 24
    property real hourHeight: 72
    property var systemPalette

    // { dayIndex, startHour, durationHours, title, subtitle }
    property var events: [
        { dayIndex: 0, startHour: 10, durationHours: 0.7, title: "Lenon's Workout", subtitle: "10:00am - 11:00am" },
        { dayIndex: 2, startHour: 10, durationHours: 1.0, title: "Lenon's Workout", subtitle: "Occurs every 1 week" },
        { dayIndex: 4, startHour: 10, durationHours: 0.7, title: "Lenon's Workout", subtitle: "Private" }
    ]

    readonly property int hours: endHour - startHour
    readonly property int quarterHourLines: hours * 4

    implicitHeight: hours * hourHeight

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(1, 1, 1, 0.02)
    }

    Repeater {
        model: root.dayCount
        delegate: Rectangle {
            required property int index
            x: Math.round(index * (root.width / root.dayCount))
            y: 0
            width: Math.ceil(root.width / root.dayCount)
            height: root.height
            color: {
                const c = root.systemPalette ? root.systemPalette.highlight : Qt.rgba(0.4, 0.6, 1.0, 1.0)
                const isWeekend = index >= 5
                const alpha = isWeekend ? 0.10 : 0.04
                return Qt.rgba(c.r, c.g, c.b, alpha)
            }
        }
    }

    Repeater {
        model: root.dayCount + 1
        delegate: Rectangle {
            required property int index
            x: Math.round(index * (root.width / root.dayCount))
            y: 0
            width: 1
            height: root.height
            color: Qt.rgba(1, 1, 1, 0.22)
        }
    }

    Repeater {
        model: root.quarterHourLines + 1
        delegate: Rectangle {
            required property int index
            x: 0
            y: Math.round(index * (root.hourHeight / 4))
            width: root.width
            height: 1
            color: {
                if (index % 4 === 0) return Qt.rgba(1, 1, 1, 0.22)   // :00
                if (index % 2 === 0) return Qt.rgba(1, 1, 1, 0.14)   // :30
                return Qt.rgba(1, 1, 1, 0.08)                        // :15/:45
            }
        }
    }

    Repeater {
        model: root.events
        delegate: Calendar.CalendarEventCard {
            required property var modelData
            readonly property real colWidth: root.width / root.dayCount

            x: modelData.dayIndex * colWidth + 3
            y: (modelData.startHour - root.startHour) * root.hourHeight + 2
            width: colWidth - 6
            height: Math.max(34, (modelData.durationHours * root.hourHeight) - 4)
            title: modelData.title
            subtitle: modelData.subtitle
        }
    }
}
