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
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Qt.rgba(1, 1, 1, 0.10)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Calendar.CalendarWeekHeader {
                Layout.fillWidth: true
                Layout.leftMargin: 58
                Layout.rightMargin: 0
                Layout.preferredHeight: 44
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
                        width: 58
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
                    }
                }

                QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }
            }
        }
    }
}
