import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../Common"

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property string eventColor: ""
    property string location: ""
    property string visibility: ""
    property string recurrence: ""
    property bool isAllDay: false

    radius: 0
    color: eventColor.length > 0 ? eventColor : KestrelColors.calendarEventDefault
    border.width: 1
    border.color: Qt.darker(color, 1.3)

    // Determine whether text should be dark or light based on background luminance.
    readonly property color bgColor: root.color
    readonly property real lum: bgColor.r * 0.299 + bgColor.g * 0.587 + bgColor.b * 0.114
    readonly property color textColor: lum > 0.55 ? KestrelColors.calendarDarkText : KestrelColors.calendarLightText
    readonly property color dimTextColor: lum > 0.55 ? "#333333" : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)

    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        anchors.topMargin: 4
        spacing: 1

        // Title
        QQC2.Label {
            Layout.fillWidth: true
            text: root.title
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideRight
            color: root.textColor
            maximumLineCount: 1
        }

        // Time
        Row {
            visible: root.subtitle.length > 0
            spacing: 3
            Kirigami.Icon {
                source: "clock"
                implicitWidth: 12
                implicitHeight: 12
                anchors.verticalCenter: parent.verticalCenter
                color: root.dimTextColor
            }
            QQC2.Label {
                text: root.subtitle
                font.pixelSize: 10
                elide: Text.ElideRight
                color: root.dimTextColor
            }
        }

        // Location
        Row {
            visible: root.location.length > 0
            spacing: 3
            Kirigami.Icon {
                source: "mark-location"
                implicitWidth: 12
                implicitHeight: 12
                anchors.verticalCenter: parent.verticalCenter
                color: root.dimTextColor
            }
            QQC2.Label {
                text: root.location
                font.pixelSize: 10
                elide: Text.ElideRight
                color: root.dimTextColor
            }
        }

        // Recurrence
        Row {
            visible: root.recurrence.length > 0
            spacing: 3
            Kirigami.Icon {
                source: "media-playlist-repeat"
                implicitWidth: 12
                implicitHeight: 12
                anchors.verticalCenter: parent.verticalCenter
                color: root.dimTextColor
            }
            QQC2.Label {
                text: root.recurrence
                font.pixelSize: 10
                elide: Text.ElideRight
                color: root.dimTextColor
            }
        }

        // Private indicator
        Row {
            visible: root.visibility === "private" || root.visibility === "confidential"
            spacing: 3
            Kirigami.Icon {
                source: "object-locked"
                implicitWidth: 12
                implicitHeight: 12
                anchors.verticalCenter: parent.verticalCenter
                color: root.dimTextColor
            }
            QQC2.Label {
                text: i18n("Private")
                font.pixelSize: 10
                color: root.dimTextColor
            }
        }

        Item { Layout.fillHeight: true }
    }
}
