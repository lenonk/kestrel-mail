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
                text: root.configData.smtpHost || root.configData.smtp_host || ""
                onEditingFinished: root.configChanged("smtpHost", text)
            }

            QQC2.Label { text: i18n("Port:"); Layout.alignment: Qt.AlignRight }
            QQC2.TextField {
                Layout.fillWidth: true
                text: (root.configData.smtpPort || root.configData.smtp_port || 587).toString()
                inputMethodHints: Qt.ImhDigitsOnly
                onEditingFinished: root.configChanged("smtpPort", parseInt(text) || 587)
            }

            QQC2.Label { text: i18n("Security policy:"); Layout.alignment: Qt.AlignRight }
            QQC2.ComboBox {
                Layout.fillWidth: true
                model: ["Use STARTTLS (always)", "Use TLS on special port", "None"]
                currentIndex: {
                    var port = parseInt(root.configData.smtpPort || root.configData.smtp_port || 587)
                    if (port === 465) return 1
                    return 0
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

                    // TODO: Wire SMTP auth toggle to backend
                    QQC2.CheckBox {
                        id: smtpAuthRequired
                        text: i18n("Server requires authentication")
                    }

                    ColumnLayout {
                        enabled: smtpAuthRequired.checked
                        Layout.leftMargin: Kirigami.Units.largeSpacing
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
        }

    }
}
