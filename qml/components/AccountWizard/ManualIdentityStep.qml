import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: identityRoot

    required property var accountSetupObj

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        spacing: 8

        QQC2.Label { text: i18n("Identity"); font.pixelSize: 18; font.bold: true }
        QQC2.Label {
            text: i18n("Enter your email address.")
            wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
        }

        Item { Layout.preferredHeight: 4 }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            QQC2.Label { text: i18n("Email address:") }
            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("you@example.com")
                text: identityRoot.accountSetupObj ? identityRoot.accountSetupObj.email : ""
                onTextEdited: if (identityRoot.accountSetupObj) identityRoot.accountSetupObj.email = text
            }
        }

        Item { Layout.fillHeight: true }
    }
}
