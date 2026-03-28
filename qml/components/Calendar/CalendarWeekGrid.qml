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

    // { dayIndex, startHour, durationHours, title, subtitle, color, location, visibility, recurrence, isAllDay }
    property var events: []

    // Separate timed vs all-day events, then compute overlap layout.
    readonly property var timedEvents: {
        const ev = events && events.length ? Array.from(events) : []
        return ev.filter(e => !e.isAllDay)
    }

    readonly property var layoutEvents: computeLayout(timedEvents)

    // Assign sub-column indices to overlapping events within each day.
    function computeLayout(evts) {
        if (!evts || evts.length === 0) return []

        var result = []
        for (var d = 0; d < dayCount; ++d) {
            // Collect events for this day.
            var dayEvts = []
            for (var i = 0; i < evts.length; ++i) {
                if ((evts[i].dayIndex || 0) === d) {
                    dayEvts.push({
                        idx: i,
                        start: evts[i].startHour || 0,
                        end: (evts[i].startHour || 0) + (evts[i].durationHours || 0.25),
                        data: evts[i]
                    })
                }
            }
            // Sort by start time, then longer events first.
            dayEvts.sort(function(a, b) {
                if (a.start !== b.start) return a.start - b.start
                return (b.end - b.start) - (a.end - a.start)
            })

            // Greedy column assignment: for each event, find the first column
            // where it doesn't overlap with any already-placed event.
            var columns = []  // columns[c] = array of events placed in column c
            for (var j = 0; j < dayEvts.length; ++j) {
                var ev = dayEvts[j]
                var placed = false
                for (var c = 0; c < columns.length; ++c) {
                    var fits = true
                    for (var k = 0; k < columns[c].length; ++k) {
                        if (ev.start < columns[c][k].end && ev.end > columns[c][k].start) {
                            fits = false
                            break
                        }
                    }
                    if (fits) {
                        columns[c].push(ev)
                        ev.subCol = c
                        placed = true
                        break
                    }
                }
                if (!placed) {
                    ev.subCol = columns.length
                    columns.push([ev])
                }
            }

            // Now determine the max columns each event shares with.
            // For each event, find all events that overlap it (directly or transitively)
            // and use the max column count from that cluster.
            var totalCols = columns.length

            // Build per-event overlap groups to get local column count.
            for (var j2 = 0; j2 < dayEvts.length; ++j2) {
                var e = dayEvts[j2]
                // Find the max subCol among events that overlap this one.
                var maxCol = e.subCol
                for (var j3 = 0; j3 < dayEvts.length; ++j3) {
                    var o = dayEvts[j3]
                    if (e.start < o.end && e.end > o.start) {
                        if (o.subCol > maxCol) maxCol = o.subCol
                    }
                }
                result.push({
                    data: e.data,
                    subCol: e.subCol,
                    totalCols: maxCol + 1
                })
            }
        }
        return result
    }

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
            color: Qt.rgba(1, 1, 1, 0.50)
        }
    }

    Repeater {
        model: root.quarterHourLines + 1
        delegate: Rectangle {
            required property int index
            x: 0
            y: Math.round(index * (root.hourHeight / 4))
            width: root.width
            height: index % 4 === 0 ? 2 : 1
            color: {
                if (index % 4 === 0) return Qt.rgba(1, 1, 1, 0.56)   // :00 (bolder)
                if (index % 2 === 0) return Qt.rgba(1, 1, 1, 0.16)   // :30 (fainter)
                return Qt.rgba(1, 1, 1, 0.09)                        // :15/:45 (faintest)
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
