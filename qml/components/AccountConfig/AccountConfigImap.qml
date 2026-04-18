import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    property var configData: ({})
    property bool isGmail: false

    signal configChanged(string key, var value)

    contentWidth: width
    contentHeight: contentColumn.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    QQC2.ScrollBar.vertical: QQC2.ScrollBar {
        width: 5
        policy: root.contentHeight > root.height ? QQC2.ScrollBar.AsNeeded : QQC2.ScrollBar.AlwaysOff
        opacity: (hovered || pressed) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    component SectionHeader: Rectangle {
        property string title: ""
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        Layout.topMargin: Kirigami.Units.largeSpacing
        implicitHeight: sectionLabel.implicitHeight + 10
        radius: 4
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.15)
        QQC2.Label {
            id: sectionLabel
            anchors.left: parent.left
            anchors.leftMargin: Kirigami.Units.largeSpacing
            anchors.verticalCenter: parent.verticalCenter
            text: title; font.bold: true; color: Kirigami.Theme.highlightColor
        }
    }

    ColumnLayout {
        id: contentColumn
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ── Server ───────────────────────────────────────────────
        SectionHeader { title: i18n("Server"); Layout.topMargin: 0 }

        GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing

            QQC2.Label { text: i18n("Host:"); Layout.alignment: Qt.AlignRight }
            QQC2.TextField {
                Layout.fillWidth: true
                text: root.configData.imapHost || root.configData.imap_host || ""
                onEditingFinished: root.configChanged("imapHost", text)
            }

            QQC2.Label { text: i18n("Port:"); Layout.alignment: Qt.AlignRight }
            QQC2.TextField {
                Layout.fillWidth: true
                text: (root.configData.imapPort || root.configData.imap_port || 993).toString()
                inputMethodHints: Qt.ImhDigitsOnly
                onEditingFinished: root.configChanged("imapPort", parseInt(text) || 993)
            }

            QQC2.Label { text: i18n("Security policy:"); Layout.alignment: Qt.AlignRight }
            QQC2.ComboBox {
                Layout.fillWidth: true
                model: ["Use TLS on special port", "Use STARTTLS (always)", "None"]
                currentIndex: {
                    var port = parseInt(root.configData.imapPort || root.configData.imap_port || 993)
                    if (port === 993) return 0
                    return 1
                }
                // TODO: Wire encryption setting to backend
            }
        }

        // ── Authentication (Generic IMAP only) ───────────────────
        Loader {
            active: !root.isGmail
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SectionHeader { title: i18n("Authentication") }

                ColumnLayout {
                    Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                    spacing: 4
                    QQC2.RadioButton { text: i18n("Use identity credentials"); checked: true }
                    QQC2.RadioButton { text: i18n("Use these credentials:") }
                    GridLayout {
                        columns: 2; columnSpacing: Kirigami.Units.largeSpacing
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        QQC2.Label { text: i18n("User name:"); Layout.alignment: Qt.AlignRight }
                        QQC2.TextField { Layout.fillWidth: true; text: root.configData.email || "" }
                        QQC2.Label { text: i18n("Password:"); Layout.alignment: Qt.AlignRight }
                        QQC2.TextField { Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: i18n("Enter password") }
                    }
                }
            }
        }

        // ── Sync Options ─────────────────────────────────────────
        SectionHeader { title: i18n("Sync Options") }

        GridLayout {
            columns: 2; columnSpacing: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing
            // TODO: Wire sync time period to backend
            QQC2.Label { text: i18n("Message sync time period:"); Layout.alignment: Qt.AlignRight; opacity: 0.7 }
            QQC2.ComboBox { Layout.fillWidth: true; model: ["All time", "Last 3 months", "Last 6 months", "Last year"] }
        }

        ColumnLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2; spacing: 4
            // TODO: Wire offline download to backend
            QQC2.CheckBox { text: i18n("Automatically download messages for offline use and search") }
            GridLayout {
                columns: 2; columnSpacing: Kirigami.Units.largeSpacing; Layout.leftMargin: Kirigami.Units.largeSpacing
                QQC2.Label { text: i18n("Download scope:"); opacity: 0.7 }
                QQC2.ComboBox { Layout.fillWidth: true; model: ["Full messages without attachments", "Full messages with attachments", "Headers only"] }
            }
            QQC2.CheckBox { text: i18n("Enable smart optimization for faster downloads"); checked: true }
            QQC2.CheckBox { text: i18n("Enable raw download (entire messages at once)") }
        }

        // ── Tags ─────────────────────────────────────────────────
        SectionHeader { title: i18n("Tags") }
        ColumnLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2; spacing: 4
            QQC2.CheckBox {
                text: root.isGmail ? i18n("Show random colors for tags downloaded from server") : i18n("Show only locally defined tags")
            }
            Loader {
                active: root.isGmail
                sourceComponent: GridLayout {
                    columns: 2; columnSpacing: Kirigami.Units.largeSpacing
                    QQC2.Label { text: i18n("Show Important tags:"); opacity: 0.7 }
                    QQC2.ComboBox { Layout.fillWidth: true; model: ["Based on Gmail's Show in IMAP setting", "Always show", "Never show"] }
                }
            }
        }

        // ── Special Folders ──────────────────────────────────────
        SectionHeader { title: i18n("Special Folders") }
        ColumnLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing; spacing: 4
            QQC2.CheckBox { id: autoDetectFolders; text: i18n("Automatically detect special folder names"); checked: true }
            GridLayout {
                columns: 2; columnSpacing: Kirigami.Units.largeSpacing; enabled: !autoDetectFolders.checked
                QQC2.Label { text: i18n("Sent:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField { Layout.fillWidth: true; text: root.isGmail ? "[Gmail]/Sent Mail" : "Sent" }
                QQC2.Label { text: i18n("Drafts:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField { Layout.fillWidth: true; text: root.isGmail ? "[Gmail]/Drafts" : "Drafts" }
                QQC2.Label { text: i18n("Trash:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField { Layout.fillWidth: true; text: root.isGmail ? "[Gmail]/Trash" : "Trash" }
                QQC2.Label { text: i18n("Junk:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField { Layout.fillWidth: true; text: root.isGmail ? "[Gmail]/Spam" : "Junk" }
                QQC2.Label { text: i18n("Archive:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField { Layout.fillWidth: true; text: root.isGmail ? "[Gmail]/All Mail" : "Archive" }
            }
        }

        // ── Generic IMAP extras ──────────────────────────────────
        Loader {
            active: !root.isGmail; Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                SectionHeader { title: i18n("Delegated accounts") }
                QQC2.Button { text: i18n("Manage delegated accounts"); Layout.leftMargin: Kirigami.Units.largeSpacing * 2 }
                SectionHeader { title: i18n("Public folders") }
                QQC2.Button { text: i18n("Manage public folders"); Layout.leftMargin: Kirigami.Units.largeSpacing * 2 }
            }
        }

        // ── Ignored paths ────────────────────────────────────────
        SectionHeader { title: i18n("Ignored paths") }
        QQC2.Label {
            text: i18n("Do not create folders from following paths:")
            opacity: 0.7; font.italic: true
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 80
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing
            color: "transparent"; border.color: Qt.darker(Kirigami.Theme.backgroundColor, 1.4); border.width: 1; radius: 4
            RowLayout {
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 24
                QQC2.Label { text: i18n("Path"); Layout.fillWidth: true; Layout.leftMargin: 8; font.bold: true; font.pointSize: Kirigami.Theme.defaultFont.pointSize - 1 }
                QQC2.Label { text: i18n("Include subfolders"); Layout.rightMargin: 8; font.bold: true; font.pointSize: Kirigami.Theme.defaultFont.pointSize - 1 }
            }
            QQC2.Label { anchors.centerIn: parent; text: i18n("There are no paths to ignore"); opacity: 0.5 }
        }
        RowLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2; spacing: Kirigami.Units.smallSpacing
            QQC2.Button { text: i18n("Add") }
            QQC2.Button { text: i18n("Edit") }
            QQC2.Button { text: i18n("Delete") }
        }

    }
}
