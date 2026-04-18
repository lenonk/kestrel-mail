import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    property var accountRepositoryObj: null
    property var accountManagerObj: null
    property string selectedEmail: ""

    signal accountSelected(string email)

    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.15)

    ListView {
        anchors.fill: parent
        model: root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
        spacing: 0
        clip: true

        delegate: Rectangle {
            id: delegateRoot
            width: ListView.view.width
            height: 56
            color: isSelected ? Kirigami.Theme.highlightColor : "transparent"

            required property var modelData
            required property int index

            readonly property string email: (modelData.email || "").toString()
            readonly property string accountName: (modelData.accountName || modelData.account_name || modelData.email || "").toString()
            readonly property string providerId: (modelData.providerId || modelData.provider_id || "generic").toString().toLowerCase()
            readonly property bool isGmail: providerId === "gmail"
            readonly property bool isSelected: root.selectedEmail === email

            readonly property string serviceSummary: {
                var parts = ["Mail"]
                if (isGmail) { parts.push("Calendar"); parts.push("Contacts") }
                return parts.join(", ")
            }

            // Get the live account's avatar source
            readonly property var liveAccount: root.accountManagerObj ? root.accountManagerObj.accountByEmail(email) : null
            readonly property string avatarIcon: liveAccount ? liveAccount.avatarSource : ""

            MouseArea {
                anchors.fill: parent
                onClicked: root.accountSelected(delegateRoot.email)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.smallSpacing + 2
                anchors.rightMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: delegateRoot.avatarIcon.length > 0 ? delegateRoot.avatarIcon : "mail-message"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    QQC2.Label {
                        text: delegateRoot.accountName
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        text: delegateRoot.serviceSummary
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize - 1
                        opacity: 0.6
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
