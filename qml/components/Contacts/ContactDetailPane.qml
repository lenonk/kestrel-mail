import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common

Rectangle {
    id: root

    property var contact: null

    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.03)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        visible: !!root.contact

        Item { Layout.preferredHeight: 8 }

        Common.AvatarBadge {
            Layout.alignment: Qt.AlignHCenter
            size: 80
            displayName: root.contact ? (root.contact.displayName || "") : ""
            avatarSources: (root.contact && root.contact.photoUrl) ? [root.contact.photoUrl] : []
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.contact ? (root.contact.displayName || "") : ""
            font.pointSize: 18
            font.bold: true
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.contact && (root.contact.organization || "").length > 0
            text: {
                if (!root.contact) { return "" }
                var parts = []
                if ((root.contact.title || "").length > 0) { parts.push(root.contact.title) }
                if ((root.contact.organization || "").length > 0) { parts.push(root.contact.organization) }
                return parts.join(" at ")
            }
            opacity: 0.7
        }

        Common.PaneDivider {
            Layout.leftMargin: -24
            Layout.rightMargin: -24
        }

        // Email section
        ColumnLayout {
            visible: root.contact && root.contact.emails && root.contact.emails.length > 0
            spacing: 6

            QQC2.Label {
                text: i18n("Email")
                font.bold: true
                opacity: 0.6
            }

            Repeater {
                model: root.contact ? (root.contact.emails || []) : []
                delegate: RowLayout {
                    spacing: 8
                    QQC2.Label {
                        text: modelData.value || ""
                        color: Kirigami.Theme.linkColor
                    }
                    QQC2.Label {
                        text: modelData.type || ""
                        opacity: 0.5
                        font.pixelSize: 11
                        visible: (modelData.type || "").length > 0
                    }
                }
            }
        }

        // Phone section
        ColumnLayout {
            visible: root.contact && root.contact.phones && root.contact.phones.length > 0
            spacing: 6

            QQC2.Label {
                text: i18n("Phone")
                font.bold: true
                opacity: 0.6
            }

            Repeater {
                model: root.contact ? (root.contact.phones || []) : []
                delegate: RowLayout {
                    spacing: 8
                    QQC2.Label {
                        text: modelData.value || ""
                    }
                    QQC2.Label {
                        text: modelData.type || ""
                        opacity: 0.5
                        font.pixelSize: 11
                        visible: (modelData.type || "").length > 0
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    QQC2.Label {
        visible: !root.contact
        anchors.centerIn: parent
        text: i18n("Select a contact")
        opacity: 0.4
        font.pixelSize: 14
    }
}
