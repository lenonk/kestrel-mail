import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: incomingRoot

    required property var accountSetupObj

    SystemPalette { id: systemPalette }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        spacing: 8

        QQC2.Label { text: i18n("Incoming server"); font.pixelSize: 18; font.bold: true }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("Incoming server"); font.pixelSize: 12 }
        }

        QQC2.Label {
            text: i18n("Select the type of incoming server you're using.")
            wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 16
            QQC2.RadioButton { text: i18n("POP3"); enabled: false }
            QQC2.RadioButton { text: i18n("IMAP"); checked: true }
        }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("Server address"); font.pixelSize: 12 }
        }

        QQC2.Label {
            text: i18n("Enter the address of your incoming mail server (for example \"mail.example.com\").")
            wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 12
            QQC2.Label { text: i18n("Incoming server:") }
            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("mail.example.com")
                text: incomingRoot.accountSetupObj ? incomingRoot.accountSetupObj.imapHost : ""
                onTextEdited: if (incomingRoot.accountSetupObj) incomingRoot.accountSetupObj.imapHost = text
            }
        }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("Authentication"); font.pixelSize: 12 }
        }

        QQC2.Label {
            text: i18n("Enter your user name (if it differs from the email address).")
            wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 12
            QQC2.Label { text: i18n("User name:") }
            QQC2.TextField {
                Layout.fillWidth: true
                text: incomingRoot.accountSetupObj
                      ? (incomingRoot.accountSetupObj.imapUsername || incomingRoot.accountSetupObj.email)
                      : ""
                onTextEdited: if (incomingRoot.accountSetupObj) incomingRoot.accountSetupObj.imapUsername = text
            }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 12
            QQC2.Label { text: i18n("Password:") }
            QQC2.TextField {
                Layout.fillWidth: true
                echoMode: TextInput.Password
                text: incomingRoot.accountSetupObj ? incomingRoot.accountSetupObj.password : ""
                onTextEdited: if (incomingRoot.accountSetupObj) incomingRoot.accountSetupObj.password = text
            }
        }

        Item { Layout.fillHeight: true }
    }
}
