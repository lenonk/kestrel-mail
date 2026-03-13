import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import ".."

Rectangle {
    id: messageCard
    visible: !isHeaderRow
    width: parent.width

    // Model properties passed in from delegate
    property var modelMessageKey
    property var modelSender
    property var modelRecipient
    property var modelSubject
    property bool modelUnread
    property int modelThreadCount
    property bool modelHasTrackingPixel
    property bool modelHasAttachments
    property bool modelIsImportant
    property var modelSnippet
    property var modelAccountEmail
    property var modelFolder
    property var modelUid
    property var modelReceivedAt
    property int modelIndex

    // Necessary for layout and logic
    property bool isHeaderRow
    property var appRoot
    property var systemPalette
    property var groupedMessageList // Pointer to the ListView

    readonly property string messageKeyValue: modelMessageKey || ""
    readonly property string selectedFolderKey: (appRoot && appRoot.selectedFolderKey) ? appRoot.selectedFolderKey.toString().toLowerCase() : ""
    readonly property string selectedFolderNorm: (appRoot && appRoot.normalizedFolderFromKey)
                                                 ? appRoot.normalizedFolderFromKey(appRoot.selectedFolderKey).toString().toLowerCase()
                                                 : ""
                                                 || ""
    readonly property bool showRecipient: selectedFolderNorm === "sent"
                                         || selectedFolderNorm === "draft"
                                         || selectedFolderNorm === "drafts"
                                         || selectedFolderNorm.indexOf("/sent") >= 0
                                         || selectedFolderNorm.indexOf("/sent ") >= 0
                                         || selectedFolderNorm.indexOf("/draft") >= 0
    readonly property string mailboxForAvatar: {
        if (showRecipient) return modelRecipient || ""
        // When thread-dedup picks the user's own reply as the latest message,
        // the sender is self — fall back to recipient for avatar lookup.
        const sEmail = appRoot ? appRoot.senderEmail(modelSender || "") : ""
        const acct   = (modelAccountEmail || "").toString().trim().toLowerCase()
        if (sEmail.length && acct.length && sEmail === acct)
            return modelRecipient || ""
        return modelSender || ""
    }
    readonly property string nameLabel: {
        if (!appRoot) return ""
        return showRecipient
                ? i18n("To: %1", appRoot.displayRecipientNames(modelRecipient, modelAccountEmail))
                : appRoot.displaySenderName(modelSender, modelAccountEmail)
    }
    height: Kirigami.Units.gridUnit * 3 + Kirigami.Units.smallSpacing + 14
    radius: 0
    clip: true
    readonly property bool isChecked: !!(appRoot && appRoot.selectedMessageKeys
                                        && appRoot.selectedMessageKeys[messageKeyValue])
    color: isChecked
           ? (systemPalette ? Qt.lighter(systemPalette.highlight, 1.35) : "transparent")
           : (appRoot && appRoot.selectedMessageKey === messageKeyValue)
             ? (systemPalette ? Qt.lighter(systemPalette.highlight, 1.18) : "transparent")
             : "transparent"
    border.color: "transparent"

    HoverHandler {
        id: rowHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    MouseArea {
        id: messageMouseArea
        anchors.fill: parent
        hoverEnabled: true

        onClicked: function(mouse) {
            if (!appRoot) return
            if (mouse.modifiers & Qt.ShiftModifier
                    && appRoot.lastClickedMessageIndex >= 0
                    && appRoot.messageListModelObj) {
                // Shift+click: range-select all message rows between anchor and here
                const from = Math.min(modelIndex, appRoot.lastClickedMessageIndex)
                const to   = Math.max(modelIndex, appRoot.lastClickedMessageIndex)
                const next = Object.assign({}, appRoot.selectedMessageKeys)
                const mdl  = appRoot.messageListModelObj
                for (let i = from; i <= to; ++i) {
                    const row = mdl.rowAt(i)
                    if (row && !row.isHeader && row.messageKey)
                        next[row.messageKey] = true
                }
                appRoot.selectedMessageKeys = next
            } else if (mouse.modifiers & Qt.ControlModifier) {
                // Ctrl+click: toggle in multiselect set without changing content view
                appRoot.lastClickedMessageIndex = modelIndex
                const next = Object.assign({}, appRoot.selectedMessageKeys)
                if (next[messageCard.messageKeyValue])
                    delete next[messageCard.messageKeyValue]
                else
                    next[messageCard.messageKeyValue] = true
                appRoot.selectedMessageKeys = next
            } else {
                // Normal click: open message, clear multiselect
                appRoot.lastClickedMessageIndex = modelIndex
                appRoot.lastMessageClickAtMs = Date.now()
                appRoot.selectedMessageKeys = ({})
                appRoot.selectedMessageKey = messageCard.messageKeyValue
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        MessageCardStatusColumn {
            // Unread dot
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 9
                height: 9
                radius: 4.5
                color: systemPalette ? systemPalette.highlight : "transparent"
                opacity: !!modelUnread ? 1 : 0.25
                visible: !!modelUnread
            }

            // Flag icon on hover
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16

                HoverHandler {
                    id: flagHover
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                }

                Kirigami.Icon {
                    anchors.fill: parent
                    source: "qrc:/qml/flag.svg"
                    isMask: true
                    opacity: rowHover.hovered ? 1.0 : 0.0
                    color: flagHover.hovered
                           ? Kirigami.Theme.textColor
                           : Qt.darker(Kirigami.Theme.textColor, 1.25)
                }
            }
        }

        MessageCardAvatar {
            appRoot: messageCard.appRoot
            mailbox: messageCard.mailboxForAvatar
            accountEmail: modelAccountEmail
            displayName: appRoot
                         ? (messageCard.showRecipient
                            ? appRoot.displayRecipientNames(modelRecipient, modelAccountEmail)
                            : appRoot.displaySenderName(modelSender, modelAccountEmail))
                         : ""
            fallbackText: messageCard.mailboxForAvatar
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                QQC2.Label {
                    text: messageCard.nameLabel
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.fillWidth: true
                    font.bold: true
                    font.pixelSize: 14
                }
                QQC2.Label {
                    text: (appRoot) ? appRoot.formatListDate(modelReceivedAt) : ""
                    opacity: 0.7
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                QQC2.Label {
                    text: (modelSubject || i18n("(No subject)"))
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    font.bold: !!modelUnread
                    font.pixelSize: 13
                    color: Kirigami.Theme.textColor
                    Layout.fillWidth: true
                }

                MessageCardIndicators {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    threadCount: modelThreadCount || 0
                    hasTrackingPixel: !!modelHasTrackingPixel
                    hasAttachments: !!modelHasAttachments
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Image {
                    visible: !!modelIsImportant
                    source: "qrc:/qml/important.svg"
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    Layout.alignment: Qt.AlignVCenter
                    fillMode: Image.PreserveAspectFit
                }

                QQC2.Label {
                    text: modelSnippet || ""
                    opacity: 0.72
                    font.pixelSize: 12
                    color: Kirigami.Theme.textColor
                    visible: text.length > 0 || !!modelIsImportant
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.fillWidth: true
                }
            }
        }

        MessageCardStatusColumn {
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16

                Kirigami.Icon {
                    anchors.fill: parent
                    source: "qrc:/qml/trash.svg"
                    isMask: true
                    opacity: rowHover.hovered ? 1.0 : 0.0
                    color: trashMouseArea.containsMouse
                           ? Kirigami.Theme.textColor
                           : Qt.darker(Kirigami.Theme.textColor, 1.25)
                }

                MouseArea {
                    id: trashMouseArea
                    anchors.fill: parent
                    enabled: rowHover.hovered
                    hoverEnabled: true
                    onClicked: function(mouse) {
                        mouse.accepted = true
                        if (appRoot)
                            appRoot.deleteSelectedMessages()
                    }
                }
            }
        }
    }
}
