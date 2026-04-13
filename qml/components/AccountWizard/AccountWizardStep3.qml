import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: step3Root

    property string avatarSource: "qrc:/qml/images/account-avatars/avatar-01.svg"

    SystemPalette { id: systemPalette }

    AvatarChooserDialog {
        id: avatarChooser
        onAvatarChosen: function(avatarPath) {
            step3Root.avatarSource = avatarPath
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        spacing: 8

        QQC2.Label { text: i18n("Almost there!"); font.pixelSize: 18; font.bold: true }
        QQC2.Label { text: i18n("When you're all set, click the Finish button to create the account."); opacity: 0.7 }

        Item { Layout.preferredHeight: 4 }

        // ── Account avatar section ────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            radius: 2
            QQC2.Label {
                anchors.left: parent.left; anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: i18n("Account avatar"); font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.leftMargin: 8
            spacing: 12

            Image {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                source: step3Root.avatarSource
                sourceSize: Qt.size(48, 48)
                fillMode: Image.PreserveAspectFit
            }

            QQC2.Label {
                text: i18n("Change...")
                color: Qt.lighter(systemPalette.highlight, 1.2)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: avatarChooser.open()
                }
            }
        }

        Item { Layout.preferredHeight: 4 }

        // ── Sync options section ──────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            radius: 2
            QQC2.Label {
                anchors.left: parent.left; anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: i18n("Sync Options"); font.pixelSize: 12
            }
        }

        // TODO: Wire up sync options once settings dialog and user preferences storage are implemented.

        RowLayout {
            Layout.leftMargin: 8
            spacing: 12
            QQC2.Label { text: i18n("Message sync time period:"); Layout.preferredWidth: 180 }
            QQC2.ComboBox {
                id: syncPeriodCombo
                Layout.preferredWidth: 200
                model: [i18n("All time"), i18n("Last year"), i18n("Last 6 months"), i18n("Last 3 months"), i18n("Last month")]
                currentIndex: 0
            }
        }

        QQC2.CheckBox {
            id: offlineCheck
            Layout.leftMargin: 8
            text: i18n("Automatically download messages for offline use and search")
            checked: false
        }

        RowLayout {
            Layout.leftMargin: 8
            spacing: 12
            QQC2.Label { text: i18n("Download scope:"); Layout.preferredWidth: 180 }
            QQC2.ComboBox {
                id: downloadScopeCombo
                Layout.preferredWidth: 260
                model: [i18n("Full messages without attachments"), i18n("Full messages with attachments"), i18n("Headers only")]
                currentIndex: 0
            }
        }

        Item { Layout.fillHeight: true }
    }
}
