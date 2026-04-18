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
    property var accountManagerObj: null

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

    readonly property var liveAccount: accountManagerObj ? accountManagerObj.accountByEmail(configData.email || "") : null

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

        // ── Connection status (Generic IMAP only) ────────────────
        Loader {
            active: !root.isGmail
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    text: i18n("On this page you can verify your account settings and attempt to fix potential problems.")
                    wrapMode: Text.Wrap; opacity: 0.7
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                }

                SectionHeader { title: i18n("IMAP") }
                RowLayout {
                    Layout.leftMargin: Kirigami.Units.largeSpacing * 2; spacing: Kirigami.Units.smallSpacing
                    Kirigami.Icon {
                        source: root.liveAccount && root.liveAccount.connected ? "dialog-ok" : "dialog-question"
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                    }
                    QQC2.Label {
                        text: root.liveAccount && root.liveAccount.connected ? i18n("Connected") : i18n("Unknown")
                    }
                }

                SectionHeader { title: i18n("SMTP") }
                RowLayout {
                    Layout.leftMargin: Kirigami.Units.largeSpacing * 2; spacing: Kirigami.Units.smallSpacing
                    Kirigami.Icon { source: "dialog-question"; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                    QQC2.Label { text: i18n("Unknown") }
                }

                // TODO: Wire Diagnose button to test IMAP+SMTP connections
                RowLayout {
                    Layout.fillWidth: true; Layout.rightMargin: Kirigami.Units.largeSpacing
                    Item { Layout.fillWidth: true }
                    QQC2.Button { text: i18n("Diagnose") }
                }
            }
        }

        // ── Diagnostic logs ──────────────────────────────────────
        SectionHeader { title: i18n("Diagnostic logs"); Layout.topMargin: root.isGmail ? Kirigami.Units.largeSpacing : 0 }

        QQC2.Label {
            text: i18n("Enable diagnostic logs for:")
            opacity: 0.7; font.italic: true
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
        }

        ColumnLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2; spacing: 4
            // TODO: Wire diagnostic log toggles to backend
            QQC2.CheckBox { text: i18n("IMAP") }
            QQC2.CheckBox { text: i18n("SMTP") }
            Loader {
                active: root.isGmail
                sourceComponent: ColumnLayout {
                    spacing: 4
                    QQC2.CheckBox { text: i18n("Google Contacts") }
                    QQC2.CheckBox { text: i18n("Google Calendar") }
                    QQC2.CheckBox { text: i18n("Google Settings") }
                }
            }
        }

        // ── Advanced Options ─────────────────────────────────────
        SectionHeader { title: i18n("Advanced Options") }

        GridLayout {
            columns: 2; columnSpacing: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing
            // TODO: Wire advanced parameters to backend
            QQC2.Label { text: i18n("Parameters:"); Layout.alignment: Qt.AlignRight; opacity: 0.7 }
            QQC2.TextField { Layout.fillWidth: true }
        }

    }
}
