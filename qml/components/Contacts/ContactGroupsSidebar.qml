import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common

Rectangle {
    id: root

    property int contactCount: 0

    color: Kirigami.Theme.backgroundColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        QQC2.Label {
            text: i18n("Contacts")
            font.bold: true
            font.pointSize: 14
        }

        Common.PaneDivider {
            Layout.leftMargin: -12
            Layout.rightMargin: -12
        }

        Rectangle {
            Layout.fillWidth: true
            height: 36
            radius: 4
            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                           Kirigami.Theme.highlightColor.b, 0.15)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Kirigami.Icon {
                    source: "user-group-properties"
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("All Contacts")
                    font.weight: Font.Medium
                }

                QQC2.Label {
                    text: root.contactCount.toString()
                    opacity: 0.6
                    font.pixelSize: 12
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
