import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: step2Root

    SystemPalette { id: systemPalette }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 12
        anchors.topMargin: 16
        spacing: 14
        QQC2.Label { text: i18n("Set up additional encryption (E2EE)"); font.pixelSize: 22; font.bold: true }
        QQC2.Label { text: i18n("Protect your communication and data with an extra layer of security via PGP end-to-end encryption technology."); wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.95 }
        QQC2.Label { text: i18n("Learn more"); color: Qt.lighter(systemPalette.highlight, 1.2) }
        Repeater {
            model: [
                { title: i18n("Create encryption keypair"), subtitle: i18n("I want to protect my email privacy with PGP encryption."), icon: "document-new" },
                { title: i18n("Import existing PGP keypair"), subtitle: i18n("I already have a keypair for this account and want to import it."), icon: "document-import" },
                { title: i18n("Continue without PGP encryption"), subtitle: i18n("I don't want to encrypt my emails for now."), icon: "object-unlocked", selected: true }
            ]
            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 108
                radius: 4
                color: modelData.selected ? Qt.lighter(systemPalette.highlight, 1.2) : "transparent"
                border.width: 1
                border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.24)
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: 38; Layout.preferredHeight: 38; opacity: 0.78 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        QQC2.Label { text: modelData.title; font.bold: true; font.pixelSize: 16 }
                        QQC2.Label { text: modelData.subtitle; wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.9 }
                    }
                }
            }
        }
    }
}
