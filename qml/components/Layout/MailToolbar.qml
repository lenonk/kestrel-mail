import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: toolbar

    required property var appRoot

    color: "transparent"

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
    }

    Item {
        anchors.fill: parent

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Kirigami.Units.smallSpacing + 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: Kirigami.Units.smallSpacing

            MailActionButton {
                iconName: "list-add"
                text: i18n("New")
                alwaysHighlighted: true
                menuItems: [
                    { text: i18n("Mail..."), icon: "mail-message-new" },
                    { text: i18n("Event..."), icon: "view-calendar-day" },
                    { text: i18n("Online Meeting...  ›"), icon: "camera-web" },
                    { text: i18n("Contact..."), icon: "contact-new" },
                    { text: i18n("Distribution List..."), icon: "im-user" },
                    { text: i18n("Task..."), icon: "view-task" },
                    { text: i18n("Note..."), icon: "document-new" },
                    { text: i18n("Chat..."), icon: "im-user-online" },
                    { text: i18n("Channel..."), icon: "network-workgroup" }
                ]
                onTriggered: (actionText) => {
                    if (actionText === "" || actionText === i18n("Mail..."))
                        toolbar.appRoot.openComposeDialog()
                }
            }
            MailActionButton {
                iconName: "view-refresh"
                text: i18n("Refresh")
                spinning: typeof accountManager !== "undefined" && accountManager.anySyncing
                onTriggered: toolbar.appRoot.syncAllAccounts()
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            spacing: Kirigami.Units.largeSpacing

            MailActionButton {
                iconName: "mail-reply-sender"
                text: i18n("Reply")
                menuItems: [
                    { text: i18n("Reply"),        icon: "mail-reply-sender" },
                    { text: i18n("Reply to all"), icon: "mail-reply-all"    }
                ]
                onTriggered: (actionText) => {
                    var d = toolbar.appRoot.selectedMessageData; if (!d) return
                    toolbar.appRoot.openReplyCompose(d, actionText === i18n("Reply to all"), toolbar.appRoot.contentPaneDarkModeEnabled)
                }
            }
            MailActionButton {
                iconName: "mail-reply-all"
                text: i18n("Reply All")
                onTriggered: {
                    var d = toolbar.appRoot.selectedMessageData; if (!d) return
                    toolbar.appRoot.openReplyCompose(d, true, toolbar.appRoot.contentPaneDarkModeEnabled)
                }
            }
            MailActionButton {
                iconName: "mail-forward"
                text: i18n("Forward")
                onTriggered: {
                    var d = toolbar.appRoot.selectedMessageData; if (!d) return
                    if (toolbar.appRoot.messageContentPane && toolbar.appRoot.messageContentPane.startAllAttachmentPrefetchForCurrentMessage)
                        toolbar.appRoot.messageContentPane.startAllAttachmentPrefetchForCurrentMessage()
                    var a = (toolbar.appRoot.messageContentPane && toolbar.appRoot.messageContentPane.forwardAttachmentPathsForCurrentMessage)
                                ? toolbar.appRoot.messageContentPane.forwardAttachmentPathsForCurrentMessage() : []
                    toolbar.appRoot.forwardMessageFromData(d, toolbar.appRoot._forwardDateText(d), a)
                }
            }
            MailActionButton { iconName: "mail-mark-important"; text: i18n("Mark"); menuItems: [{ text: i18n("Read"), icon: "mail-mark-read" }, { text: i18n("Unread"), icon: "mail-mark-unread" }] }
            MailActionButton { iconName: "archive-insert"; text: i18n("Archive") }
            MailActionButton { iconName: "edit-delete"; text: i18n("Delete"); onTriggered: toolbar.appRoot.deleteSelectedMessages() }
        }
    }
}
