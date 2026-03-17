import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "." as Calendar

Item {
    id: root

    property int startHour: 8
    property int endHour: 18
    property real baseHourHeight: 72
    readonly property int hoursVisible: Math.max(1, endHour - startHour)
    readonly property real computedHourHeight: Math.max(baseHourHeight, calendarScroll.height / hoursVisible)

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
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Qt.rgba(1, 1, 1, 0.10)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Calendar.CalendarTimeGutter {
                Layout.fillHeight: true
                Layout.preferredWidth: 58
                startHour: root.startHour
                endHour: root.endHour
                hourHeight: root.computedHourHeight
                topInset: weekHeader.height
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Calendar.CalendarWeekHeader {
                    id: weekHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                }

                Flickable {
                    id: calendarScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: weekGrid.implicitHeight

                    Calendar.CalendarWeekGrid {
                        id: weekGrid
                        width: parent.width
                        startHour: root.startHour
                        endHour: root.endHour
                        hourHeight: root.computedHourHeight
                    }

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }
                }
            }
        }
    }
}
