import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "." as Calendar

Item {
    id: root

    property int startHour: 0
    property int endHour: 24
    property real hourHeight: 108
    property var systemPalette
    property var allEvents: []
    property var visibleCalendarIds: []
    readonly property real gutterWidth: gutter.implicitWidth

    readonly property var filteredEvents: {
        const ev = allEvents && allEvents.length ? Array.from(allEvents) : []
        const visible = visibleCalendarIds && visibleCalendarIds.length ? Array.from(visibleCalendarIds) : []
        if (visible.length === 0) return []
        return ev.filter(e => visible.indexOf(String(e.calendarId || "")) >= 0)
    }

    readonly property var allDayEvents: {
        const ev = filteredEvents && filteredEvents.length ? filteredEvents : []
        const allDay = ev.filter(e => !!e.isAllDay)
        // Sort by dayIndex first, then wider spans before narrower (stable stacking).
        allDay.sort((a, b) => {
            const da = a.dayIndex || 0, db = b.dayIndex || 0
            if (da !== db) return da - db
            return (b.spanDays || 1) - (a.spanDays || 1)
        })
        return allDay
    }

    readonly property var timedEvents: {
        const ev = filteredEvents && filteredEvents.length ? filteredEvents : []
        return ev.filter(e => !e.isAllDay)
    }

    // Week date computation — derive from current week offset (TODO: wire to nav buttons).
    property int weekOffset: 0
    readonly property date _weekMonday: {
        const now = new Date()
        const day = now.getDay()
        const diff = (day === 0 ? -6 : 1 - day) + weekOffset * 7
        return new Date(now.getFullYear(), now.getMonth(), now.getDate() + diff)
    }
    readonly property var _dayNumbers: {
        var nums = []
        for (var i = 0; i < 7; ++i) {
            var d = new Date(_weekMonday)
            d.setDate(d.getDate() + i)
            nums.push(d.getDate())
        }
        return nums
    }
    readonly property int _todayIndex: {
        const now = new Date()
        for (var i = 0; i < 7; ++i) {
            var d = new Date(_weekMonday)
            d.setDate(d.getDate() + i)
            if (d.getFullYear() === now.getFullYear() &&
                d.getMonth() === now.getMonth() &&
                d.getDate() === now.getDate())
                return i
        }
        return -1
    }
    readonly property string _rangeLabel: {
        const mon = _weekMonday
        const sun = new Date(mon)
        sun.setDate(sun.getDate() + 6)
        const opts = { month: "long" }
        const monMonth = mon.toLocaleDateString(Qt.locale(), "MMMM")
        const sunMonth = sun.toLocaleDateString(Qt.locale(), "MMMM")
        if (monMonth === sunMonth)
            return monMonth + " " + mon.getDate() + " - " + sun.getDate() + ", " + mon.getFullYear()
        return monMonth + " " + mon.getDate() + " - " + sunMonth + " " + sun.getDate() + ", " + sun.getFullYear()
    }

    function scrollToEightAm() {
        const target = 8 * root.hourHeight
        const maxY = Math.max(0, calendarScroll.contentHeight - calendarScroll.height)
        calendarScroll.contentY = Math.max(0, Math.min(target, maxY))
    }

    onVisibleChanged: {
        if (visible) {
            Qt.callLater(scrollToEightAm)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.darker(Kirigami.Theme.backgroundColor, 1.06)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        Calendar.CalendarTopBar {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            rangeLabel: root._rangeLabel
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.10)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Calendar.CalendarWeekHeader {
                Layout.fillWidth: true
                Layout.leftMargin: root.gutterWidth
                Layout.rightMargin: 0
                Layout.preferredHeight: 36
                dayNumbers: root._dayNumbers
                todayIndex: root._todayIndex
                systemPalette: root.systemPalette
            }

            Calendar.CalendarAllDayRow {
                Layout.fillWidth: true
                dayCount: 7
                allDayEvents: root.allDayEvents
                systemPalette: root.systemPalette
                gutterWidth: root.gutterWidth
            }

            Flickable {
                id: calendarScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: scrollContent.width
                contentHeight: scrollContent.height

                Row {
                    id: scrollContent
                    width: calendarScroll.width
                    height: Math.max(gutter.implicitHeight, weekGrid.implicitHeight)

                    Calendar.CalendarTimeGutter {
                        id: gutter
                        width: gutter.implicitWidth
                        startHour: root.startHour
                        endHour: root.endHour
                        hourHeight: root.hourHeight
                    }

                    Calendar.CalendarWeekGrid {
                        id: weekGrid
                        width: Math.max(0, scrollContent.width - gutter.width)
                        startHour: root.startHour
                        endHour: root.endHour
                        hourHeight: root.hourHeight
                        systemPalette: root.systemPalette
                        events: root.filteredEvents
                    }
                }

                QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                    width: 5
                    policy: QQC2.ScrollBar.AsNeeded
                    background: Item {}
                    contentItem: Rectangle {
                        implicitWidth: 5
                        readonly property color sColor: Kirigami.Theme.textColor
                        color: Qt.rgba(sColor.r, sColor.g, sColor.b, 0.75)
                    }
                }
            }
        }
    }
}
