import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

Item {
    id: root

    property int dayCount: 7
    property var allDayEvents: []   // filtered to isAllDay === true

    implicitHeight: {
        // At least one row height (28), expand if multiple events stacked on one day.
        const minH = 28
        if (!Array.isArray(allDayEvents) || allDayEvents.length === 0)
            return minH
        // Count max events per day.
        var maxPerDay = 0
        var counts = []
        for (var d = 0; d < dayCount; ++d) counts.push(0)
        for (var i = 0; i < allDayEvents.length; ++i) {
            var di = allDayEvents[i].dayIndex || 0
            if (di >= 0 && di < dayCount) {
                counts[di]++
                if (counts[di] > maxPerDay) maxPerDay = counts[di]
            }
        }
        return Math.max(minH, maxPerDay * 24 + 4)
    }

    // "All-day" label on the left
    QQC2.Label {
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        text: "All-day"
        font.pixelSize: 10
        font.italic: true
        color: Qt.rgba(1, 1, 1, 0.50)
        width: 44
    }

    // Day columns background + dividers
    Item {
        x: 48
        width: parent.width - 48
        height: parent.height

        // Vertical dividers
        Repeater {
            model: root.dayCount + 1
            delegate: Rectangle {
                required property int index
                x: Math.round(index * (parent.width / root.dayCount)) - 1
                y: 0
                width: 1
                height: parent.height
                color: Qt.rgba(1, 1, 1, 0.20)
            }
        }

        // All-day event chips
        Repeater {
            model: root.allDayEvents
            delegate: Rectangle {
                required property var modelData
                required property int index

                // Stack events in same day column.
                readonly property real colWidth: parent.width / root.dayCount
                readonly property int dayIdx: modelData.dayIndex || 0
                // Count how many all-day events appear before this one on the same day.
                readonly property int stackIdx: {
                    var n = 0
                    for (var i = 0; i < index; ++i) {
                        if ((root.allDayEvents[i].dayIndex || 0) === dayIdx)
                            n++
                    }
                    return n
                }

                x: dayIdx * colWidth + 2
                y: stackIdx * 24 + 2
                width: colWidth - 4
                height: 22
                radius: 3
                color: modelData.color || "#9a8cff"
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
                    color: lum > 0.55 ? "#111111" : "#ffffff"
                }
            }
        }
    }

    // Bottom border
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Qt.rgba(1, 1, 1, 0.20)
    }
}
