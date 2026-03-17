import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".." as Components

ColumnLayout {
    id: root

    property bool visibleInCalendar: false
    property bool gmailCalendarsExpanded: true
    property var calendarSources: []

    signal expandedToggled(bool expanded)
    signal sourceToggled(string sourceId, bool checked)

    visible: visibleInCalendar
    Layout.fillWidth: true
    spacing: Kirigami.Units.mediumSpacing

    Components.FolderSectionButton {
        expanded: root.gmailCalendarsExpanded
        sectionIcon: "internet-mail"
        title: i18n("Gmail")
        titleOpacity: 0.9
        rowHeight: Kirigami.Units.gridUnit + Kirigami.Units.largeSpacing
        chevronSize: 16
        sectionIconSize: 24

        onActivated: {
            const next = !root.gmailCalendarsExpanded
            root.gmailCalendarsExpanded = next
            root.expandedToggled(next)
        }
    }

    Repeater {
        model: root.gmailCalendarsExpanded ? root.calendarSources.filter(c => (c.account || "") === "gmail") : []
        delegate: QQC2.CheckBox {
            id: calendarCheck
            required property var modelData
            Layout.fillWidth: true
            checked: !!modelData.checked
            leftPadding: Kirigami.Units.largeSpacing
            rightPadding: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing
            text: modelData.name
            onToggled: root.sourceToggled(modelData.id, checked)

            indicator: Rectangle {
                implicitWidth: 16
                implicitHeight: 16
                x: calendarCheck.leftPadding
                y: (calendarCheck.height - height) / 2
                radius: 3
                color: modelData.color || Qt.rgba(1, 1, 1, 0.12)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.45)

                QQC2.Label {
                    anchors.centerIn: parent
                    text: "✓"
                    visible: calendarCheck.checked
                    font.bold: true
                    font.pixelSize: 20
                    color: Qt.rgba(0, 0, 0, 1)
                }
            }

            contentItem: QQC2.Label {
                text: calendarCheck.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: calendarCheck.indicator.width + calendarCheck.spacing
                color: Kirigami.Theme.textColor
            }
        }
    }

    Item { Layout.fillHeight: true }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        QQC2.Label {
            text: Qt.formatDate(new Date(), "MMMM yyyy")
            font.bold: true
            opacity: 0.85
            leftPadding: 8
        }

        GridLayout {
            columns: 7
            columnSpacing: 4
            rowSpacing: 4
            Layout.fillWidth: true

            Repeater {
                model: ["S", "M", "T", "W", "T", "F", "S"]
                delegate: QQC2.Label {
                    required property var modelData
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    opacity: 0.7
                    font.pixelSize: 11
                }
            }

            Repeater {
                model: 35
                delegate: QQC2.Label {
                    required property int index
                    readonly property date nowDate: new Date()
                    readonly property date firstDay: new Date(nowDate.getFullYear(), nowDate.getMonth(), 1)
                    readonly property int startWeekday: firstDay.getDay()
                    readonly property int dayNum: index - startWeekday + 1
                    readonly property int daysInMonth: new Date(nowDate.getFullYear(), nowDate.getMonth() + 1, 0).getDate()
                    readonly property bool inMonth: dayNum >= 1 && dayNum <= daysInMonth
                    text: inMonth ? dayNum : ""
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    opacity: inMonth ? 0.9 : 0
                    font.pixelSize: 11
                }
            }
        }

        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
    }
}
