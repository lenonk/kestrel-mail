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

    // Account avatar from live IAccount
    readonly property var liveAccount: accountManagerObj ? accountManagerObj.accountByEmail(configData.email || "") : null
    readonly property string accountAvatar: liveAccount ? liveAccount.avatarSource : ""

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
            text: title
            font.bold: true
            color: Kirigami.Theme.highlightColor
        }
    }

    ColumnLayout {
        id: contentColumn
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ── Identity fields + account avatar ─────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing * 2
            spacing: Kirigami.Units.largeSpacing

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true

                QQC2.Label { text: i18n("Account name:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField {
                    Layout.fillWidth: true
                    text: root.configData.accountName || root.configData.account_name || ""
                    onEditingFinished: root.configChanged("accountName", text)
                }

                QQC2.Label { text: i18n("Name:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField {
                    Layout.fillWidth: true
                    text: root.configData.displayName || root.configData.display_name || ""
                    onEditingFinished: root.configChanged("displayName", text)
                }

                QQC2.Label { text: i18n("Email:"); Layout.alignment: Qt.AlignRight }
                QQC2.TextField {
                    Layout.fillWidth: true
                    text: root.configData.email || ""
                    readOnly: true
                    opacity: 0.7
                }
            }

            Kirigami.Icon {
                source: root.accountAvatar.length > 0 ? root.accountAvatar : "mail-message"
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                Layout.alignment: Qt.AlignTop
            }
        }

        // ── Copies ───────────────────────────────────────────────
        SectionHeader { title: i18n("Copies") }

        GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing

            QQC2.Label { text: i18n("Bcc address:"); Layout.alignment: Qt.AlignRight }
            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("Auto-Bcc address")
                // TODO: Wire to backend -- bcc_address field not in accounts table yet
            }
        }

        // ── Gmail: Services ──────────────────────────────────────
        Loader {
            active: root.isGmail
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SectionHeader { title: i18n("Services") }

                ColumnLayout {
                    Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                    spacing: 4

                    QQC2.CheckBox { text: i18n("IMAP"); checked: true; enabled: false }
                    QQC2.CheckBox { text: i18n("SMTP"); checked: true; enabled: false }
                    // TODO: Wire Google Contacts toggle to backend
                    QQC2.CheckBox { text: i18n("Google Contacts"); checked: true }
                    // TODO: Wire Google Calendar toggle to backend
                    QQC2.CheckBox { text: i18n("Google Calendar"); checked: true }
                    // TODO: Wire Google Settings toggle to backend
                    QQC2.CheckBox { text: i18n("Google Settings"); checked: true }
                }

                // TODO: Wire "Include when sending/receiving" to backend
                QQC2.CheckBox {
                    text: i18n("Include when sending/receiving emails")
                    checked: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                }
            }
        }

        // ── Generic IMAP: Authentication ─────────────────────────
        Loader {
            active: !root.isGmail
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SectionHeader { title: i18n("Authentication") }

                GridLayout {
                    columns: 2
                    columnSpacing: Kirigami.Units.largeSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing

                    QQC2.Label { text: i18n("Login name:"); Layout.alignment: Qt.AlignRight }
                    QQC2.TextField {
                        Layout.fillWidth: true
                        text: root.configData.email || ""
                        // TODO: Wire separate IMAP username if different from email
                    }

                    QQC2.Label { text: i18n("Password:"); Layout.alignment: Qt.AlignRight }
                    QQC2.TextField {
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: i18n("Enter password")
                        // TODO: Save new password via TokenVault on editing finished
                    }
                }

                // ── Default Folders ──
                SectionHeader { title: i18n("Default Folders") }

                GridLayout {
                    columns: 3
                    columnSpacing: Kirigami.Units.largeSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing

                    // TODO: Wire default folder selection to backend
                    QQC2.Label { text: i18n("Calendar:"); Layout.alignment: Qt.AlignRight }
                    QQC2.TextField { Layout.fillWidth: true }
                    QQC2.Button { text: i18n("Select...") }

                    QQC2.Label { text: i18n("Tasks:"); Layout.alignment: Qt.AlignRight }
                    QQC2.TextField { Layout.fillWidth: true }
                    QQC2.Button { text: i18n("Select...") }

                    QQC2.Label { text: i18n("Contacts:"); Layout.alignment: Qt.AlignRight }
                    QQC2.TextField { Layout.fillWidth: true }
                    QQC2.Button { text: i18n("Select...") }
                }
            }
        }

        // ── Copies (shown for both, but also for generic IMAP) ───
        Loader {
            active: !root.isGmail
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SectionHeader { title: i18n("Send") }

                // TODO: Wire "save sent copy" toggle to backend
                QQC2.CheckBox {
                    text: i18n("Save copy of sent messages to \"Sent\" folder")
                    checked: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                }
            }
        }

    }
}
