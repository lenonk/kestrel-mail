import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: outgoingRoot

    required property var accountSetupObj
    property string smtpUsername: ""
    property string smtpPassword: ""
    property bool noSmtpAuth: false

    SystemPalette { id: systemPalette }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        spacing: 8

        QQC2.Label { text: i18n("Outgoing server"); font.pixelSize: 18; font.bold: true }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("Server address"); font.pixelSize: 12 }
        }

        QQC2.Label {
            text: i18n("Enter the address of your outgoing (SMTP) mail server.")
            wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 12
            QQC2.Label { text: i18n("Outgoing server:") }
            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("smtp.example.com")
                text: outgoingRoot.accountSetupObj ? outgoingRoot.accountSetupObj.smtpHost : ""
                onTextEdited: if (outgoingRoot.accountSetupObj) outgoingRoot.accountSetupObj.smtpHost = text
            }
        }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("Authentication"); font.pixelSize: 12 }
        }

        QQC2.Label {
            text: i18n("Enter your user name (if it differs from the incoming user name).")
            wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 12
            visible: !outgoingRoot.noSmtpAuth
            QQC2.Label { text: i18n("User name:") }
            QQC2.TextField {
                Layout.fillWidth: true
                text: outgoingRoot.smtpUsername || (outgoingRoot.accountSetupObj ? outgoingRoot.accountSetupObj.email : "")
                onTextEdited: outgoingRoot.smtpUsername = text
            }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 12
            visible: !outgoingRoot.noSmtpAuth
            QQC2.Label { text: i18n("Password:") }
            QQC2.TextField {
                Layout.fillWidth: true
                echoMode: TextInput.Password
                text: outgoingRoot.smtpPassword
                onTextEdited: outgoingRoot.smtpPassword = text
            }
        }

        QQC2.CheckBox {
            Layout.leftMargin: 20
            text: i18n("Outgoing server doesn't require authentication")
            checked: outgoingRoot.noSmtpAuth
            onToggled: outgoingRoot.noSmtpAuth = checked
        }

        Item { Layout.fillHeight: true }
    }
}
