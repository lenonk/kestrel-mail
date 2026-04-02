import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "." as Calendar

Item {
    id: root

    property int dayCount: 7
    property int startHour: 0
    property int endHour: 24
    property real hourHeight: 72
    property var systemPalette

    // { dayIndex, startHour, durationHours, title, subtitle, color, location, visibility, recurrence, isAllDay }
    property var events: []

    // Separate timed vs all-day events, then compute overlap layout.
    readonly property var timedEvents: {
        const ev = events && events.length ? Array.from(events) : []
        return ev.filter(e => !e.isAllDay)
    }

    readonly property var layoutEvents: calendarLayoutHelper.computeLayout(timedEvents, dayCount)

    readonly property int hours: endHour - startHour
    readonly property int quarterHourLines: hours * 4

    implicitHeight: hours * hourHeight

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.02)
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
                const alpha = isWeekend ? 0.04 : 0.10
                return Qt.rgba(c.r, c.g, c.b, alpha)
            }
        }
    }

    Repeater {
        model: root.dayCount + 1
        delegate: Rectangle {
            required property int index
            x: Math.round(index * (root.width / root.dayCount)) - 1
            y: 0
            width: 1
            height: root.height
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.40)
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
                const t = Kirigami.Theme.textColor
                if (index % 4 === 0) return Qt.rgba(t.r, t.g, t.b, 0.40)  // :00
                if (index % 2 === 0) return Qt.rgba(t.r, t.g, t.b, 0.08)  // :30
                return Qt.rgba(t.r, t.g, t.b, 0.04)                       // :15/:45
            }
        }
    }

    Repeater {
        model: root.layoutEvents
        delegate: Calendar.CalendarEventCard {
            required property var modelData
            readonly property real dayColWidth: root.width / root.dayCount
            readonly property int dayIdx: modelData.data.dayIndex || 0
            readonly property int subCol: modelData.subCol || 0
            readonly property int totalCols: modelData.totalCols || 1
            readonly property real subColWidth: (dayColWidth - 4) / totalCols

            x: dayIdx * dayColWidth + 2 + subCol * subColWidth
            y: ((modelData.data.startHour || 0) - root.startHour) * root.hourHeight + 2
            width: subColWidth - 2
            height: Math.max(34, ((modelData.data.durationHours || 0.25) * root.hourHeight) - 4)
            title: modelData.data.title || ""
            subtitle: modelData.data.subtitle || ""
            eventColor: modelData.data.color || ""
            location: modelData.data.location || ""
            visibility: modelData.data.visibility || ""
            recurrence: modelData.data.recurrence || ""
            isAllDay: false
        }
    }
}
