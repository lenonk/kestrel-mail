import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root

    property int startHour: 0
    property int endHour: 24
    property real hourHeight: 72

    readonly property int totalSlots: (endHour - startHour) * 2   // every 30 min

    // Measure the widest possible time string to size the gutter dynamically.
    Text {
        id: gutterMetric
        visible: false
        text: "00"
        font.pixelSize: 19
        font.family: condensedFontFamily
    }
    Text {
        id: minuteMetric
        visible: false
        text: "00"
        font.pixelSize: 13
        font.family: condensedFontFamily
    }
    Text {
        id: allDayMetric
        visible: false
        text: "All-day"
        font.pixelSize: 12
    }

    readonly property real timeWidth: gutterMetric.implicitWidth + minuteMetric.implicitWidth
                                      + Kirigami.Units.smallSpacing + 12
    implicitWidth: Math.max(timeWidth, allDayMetric.implicitWidth + 12)
    implicitHeight: (endHour - startHour) * hourHeight

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        Repeater {
            model: root.totalSlots + 1

            delegate: Item {
                required property int index
                visible: index > 0 && index < root.totalSlots

                readonly property int half: index
                readonly property int hour24: root.startHour + Math.floor(half / 2)
                readonly property bool isHalf: (half % 2) === 1
                readonly property int h12: hour24 % 12 === 0 ? 12 : hour24 % 12
                readonly property string hourStr: (h12 < 10 ? "0" : "") + h12
                readonly property string minStr: isHalf ? "30" : "00"

                x: 0
                y: index * (root.hourHeight / 2)
                width: root.implicitWidth
                height: 20

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    spacing: Kirigami.Units.smallSpacing

                    // Hour digits
                    Text {
                        text: hourStr
                        font.pixelSize: 19
                        font.family: condensedFontFamily
                        color: Kirigami.Theme.textColor
                    }

                    // Minute digits (superscript style)
                    Text {
                        text: minStr
                        font.pixelSize: 13
                        font.family: condensedFontFamily
                        color: Kirigami.Theme.textColor
                        anchors.top: parent.top
                    }
                }
            }
        }
    }
}
