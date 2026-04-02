import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

Item {
    id: root

    property int dayCount: 7
    property var allDayEvents: []   // filtered to isAllDay === true
    property var systemPalette
    property real gutterWidth: 58

    implicitHeight: {
        // At least one row height (28), expand if multiple events stacked on one day.
        const minH = 28
        if (!Array.isArray(allDayEvents) || allDayEvents.length === 0)
            return minH
        // Count max events covering each day (spanning events cover multiple days).
        var counts = []
        for (var d = 0; d < dayCount; ++d) counts.push(0)
        for (var i = 0; i < allDayEvents.length; ++i) {
            var di = allDayEvents[i].dayIndex || 0
            var span = allDayEvents[i].spanDays || 1
            for (var s = di; s < di + span && s < dayCount; ++s)
                counts[s]++
        }
        var maxPerDay = 0
        for (var d2 = 0; d2 < dayCount; ++d2)
            if (counts[d2] > maxPerDay) maxPerDay = counts[d2]
        return Math.max(minH, maxPerDay * 24 + 4)
    }

    // "All-day" label on the left (aligned with 58px time gutter)
    QQC2.Label {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: allDayColumns.left
        anchors.rightMargin: 6
        text: i18n("All-day")
        font.pixelSize: 12
        color: Kirigami.Theme.textColor
    }

    // Day columns background + dividers
    Item {
        id: allDayColumns
        x: root.gutterWidth
        width: parent.width - root.gutterWidth
        height: parent.height

        // Day column backgrounds (match week grid)
        Repeater {
            model: root.dayCount
            delegate: Rectangle {
                required property int index
                x: Math.round(index * (parent.width / root.dayCount))
                y: 0
                width: Math.ceil(parent.width / root.dayCount)
                height: parent.height
                color: {
                    const c = root.systemPalette ? root.systemPalette.highlight : Qt.rgba(0.4, 0.6, 1.0, 1.0)
                    const isWeekend = index >= 5
                    const alpha = isWeekend ? 0.04 : 0.10
                    return Qt.rgba(c.r, c.g, c.b, alpha)
                }
            }
        }

        // Vertical dividers
        Repeater {
            model: root.dayCount + 1
            delegate: Rectangle {
                required property int index
                x: Math.round(index * (parent.width / root.dayCount)) - 1
                y: 0
                width: 1
                height: parent.height
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.40)
            }
        }

        // All-day event chips (may span multiple day columns)
        Repeater {
            model: root.allDayEvents
            delegate: Rectangle {
                required property var modelData
                required property int index

                readonly property real colWidth: parent.width / root.dayCount
                readonly property int dayIdx: modelData.dayIndex || 0
                readonly property int span: modelData.spanDays || 1
                // Count how many earlier events overlap this event's first day.
                readonly property int stackIdx: {
                    var n = 0
                    for (var i = 0; i < index; ++i) {
                        var oDayIdx = root.allDayEvents[i].dayIndex || 0
                        var oSpan = root.allDayEvents[i].spanDays || 1
                        if (oDayIdx <= dayIdx && oDayIdx + oSpan > dayIdx)
                            n++
                    }
                    return n
                }

                x: dayIdx * colWidth + 2
                y: stackIdx * 24 + 2
                width: colWidth * span - 4
                height: 22
                radius: 0
                color: modelData.color || KestrelColors.calendarEventDefault
                border.width: 1
                border.color: Qt.darker(color, 1.3)

                QQC2.Label {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    text: modelData.title || ""
                    font.pixelSize: 10
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    // Contrast-aware text color
                    readonly property real lum: parent.color.r * 0.299 + parent.color.g * 0.587 + parent.color.b * 0.114
                    color: lum > 0.55 ? KestrelColors.calendarDarkText : KestrelColors.calendarLightText
                }
            }
        }
    }

    // Bottom border (separates all-day from timed grid)
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 2
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.60)
    }
}
