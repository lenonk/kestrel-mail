import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    required property var appRoot
    required property var systemPalette

    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.02)

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        anchors.topMargin: Kirigami.Units.smallSpacing + 2
        anchors.bottomMargin: 0
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            visible: root.appRoot.selectedFolderCategories.length > 0
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: root.appRoot.selectedFolderCategories
                delegate: Item {
                    implicitWidth: categoryLabel.implicitWidth + 50
                    implicitHeight: Math.round((categoryLabel.implicitHeight + 3) * 1.5)

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: false
                        onClicked: {
                            root.appRoot.categorySelectionExplicit = true
                            root.appRoot.selectedCategoryIndex = index
                        }
                    }

                    QQC2.Label {
                        id: categoryLabel
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        font.bold: index === root.appRoot.selectedCategoryIndex
                        opacity: index === root.appRoot.selectedCategoryIndex ? 1 : 0.75
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: index === root.appRoot.selectedCategoryIndex ? root.systemPalette.highlight : "transparent"
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        QQC2.Label {
            visible: (typeof kestrelDebugBuild !== "undefined") && !!kestrelDebugBuild
            Layout.fillWidth: true
            leftPadding: Kirigami.Units.smallSpacing
            rightPadding: Kirigami.Units.smallSpacing
            bottomPadding: 2
            opacity: 0.7
            elide: Text.ElideRight
            font.pointSize: Math.max(8, Kirigami.Theme.defaultFont.pointSize - 1)
            text: {
                const m = root.appRoot.messageListModelObj
                if (!m) return ""
                return i18n("Displayed: %1 messages (%2 rows) · Built: %3 rows · Page: %4 · %5",
                            m.visibleMessageCount,
                            m.visibleRowCount,
                            m.totalRowCount,
                            m.pageSize,
                            m.hasMore ? i18n("more available") : i18n("end"))
            }
        }

        ListView {
            id: groupedMessageList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.appRoot.messageListModelObj ? root.appRoot.messageListModelObj : []
            clip: true
            spacing: 0

            property real preservedContentY: 0
            property bool restorePending: false

            property string debugHoverPayload: ""

            function showDebugHoverPopup(payload, globalX, globalY) {
                debugHoverPayload = payload
                debugHoverPopup.toolTipText = payload
                debugHoverPopup.preferredX = globalX + 12
                debugHoverPopup.preferredY = globalY + 12
                debugHoverPopup.show()
            }

            function hideDebugHoverPopup() {
                debugHoverPopup.hide()
            }

            function queueRestoreScroll() {
                if (!restorePending)
                    return
                Qt.callLater(function() {
                    const maxY = Math.max(0, groupedMessageList.contentHeight - groupedMessageList.height)
                    groupedMessageList.contentY = Math.max(0, Math.min(maxY, groupedMessageList.preservedContentY))
                    groupedMessageList.restorePending = false
                })
            }

            Connections {
                target: root.appRoot.messageListModelObj
                ignoreUnknownSignals: true

                function onModelAboutToBeReset() {
                    groupedMessageList.preservedContentY = groupedMessageList.contentY
                    groupedMessageList.restorePending = true
                }

                function onModelReset() {
                    groupedMessageList.queueRestoreScroll()
                }

                function onRowsAboutToBeInserted() {
                    groupedMessageList.preservedContentY = groupedMessageList.contentY
                    groupedMessageList.restorePending = true
                }

                function onRowsAboutToBeRemoved() {
                    groupedMessageList.preservedContentY = groupedMessageList.contentY
                    groupedMessageList.restorePending = true
                }

                function onRowsInserted() {
                    groupedMessageList.queueRestoreScroll()
                }

                function onRowsRemoved() {
                    groupedMessageList.queueRestoreScroll()
                }
            }

            onContentHeightChanged: queueRestoreScroll()

            TextToolTip {
                id: debugHoverPopup
                delay: 0
                clampToOverlay: true

                contentItem: Column {
                    spacing: 6

                    QQC2.Label {
                        text: groupedMessageList.debugHoverPayload
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 6
                        bottomPadding: 0
                        font.family: "monospace"
                    }

                    QQC2.Button {
                        text: i18n("Copy")
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.bottomMargin: 6
                        onClicked: {
                            copyBuffer.text = groupedMessageList.debugHoverPayload
                            copyBuffer.selectAll()
                            copyBuffer.copy()
                        }
                    }
                }

                TextEdit {
                    id: copyBuffer
                    visible: false
                }
            }

            Rectangle {
                anchors.fill: parent
                z: -1
                color: Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
            }

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                width: 5
                id: listScrollBar
                policy: QQC2.ScrollBar.AsNeeded
                opacity: (hovered || pressed) ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            WheelHandler {
                target: groupedMessageList
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                property real wheelBoost: 2.2
                onWheel: function(event) {
                    const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : (event.angleDelta.y / 120.0) * 40
                    groupedMessageList.contentY = Math.max(0,
                        Math.min(groupedMessageList.contentHeight - groupedMessageList.height,
                                 groupedMessageList.contentY - (dy * wheelBoost)))
                    event.accepted = true
                }
            }

            onContentYChanged: {
                if (!root.appRoot.messageListModelObj) return
                const bottomGap = contentHeight - (contentY + height)
                if (bottomGap < 1800) {
                    root.appRoot.messageListModelObj.loadMore()
                }
            }

            delegate: Item {
                width: groupedMessageList.width
                readonly property bool isHeaderRow: !!isHeader
                readonly property int topGap: 0
                implicitHeight: topGap + (headerButton.visible ? (headerButton.implicitHeight + 10) : messageCard.height)

                Item {
                    x: 0
                    y: topGap
                    width: parent.width
                    height: parent.implicitHeight - topGap

                    Rectangle {
                        anchors.fill: parent
                        color: Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                    }

                    Rectangle {
                        visible: isHeaderRow && hasTopGap
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 2
                        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.10)
                        opacity: 0.55
                    }

                    QQC2.Button {
                        id: headerButton
                        visible: isHeaderRow
                        width: parent.width
                        implicitHeight: Kirigami.Units.gridUnit + 2
                        y: 10
                        flat: true
                        leftPadding: 0
                        rightPadding: 0
                        hoverEnabled: false
                        background: Item {}
                        contentItem: RowLayout {
                            anchors.fill: parent
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                source: expanded ? "go-down-symbolic" : "go-next-symbolic"
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                            }
                            QQC2.Label { text: title || ""; font.bold: true }
                            Item { Layout.fillWidth: true }
                        }
                        onClicked: root.appRoot.setBucketExpanded(bucketKey, !expanded)
                    }

                    Rectangle {
                        id: messageCard
                        visible: !isHeaderRow
                        width: parent.width
                        readonly property string messageKeyValue: messageKey || ""
                        readonly property string selectedFolderKey: (root.appRoot && root.appRoot.selectedFolderKey) ? root.appRoot.selectedFolderKey.toString().toLowerCase() : ""
                        readonly property string selectedFolderNorm: (root.appRoot && root.appRoot.normalizedFolderFromKey)
                                                                     ? root.appRoot.normalizedFolderFromKey(root.appRoot.selectedFolderKey).toString().toLowerCase()
                                                                     : ""
                        readonly property bool showRecipient: selectedFolderNorm === "sent"
                                                             || selectedFolderNorm === "draft"
                                                             || selectedFolderNorm === "drafts"
                                                             || selectedFolderNorm.indexOf("/sent") >= 0
                                                             || selectedFolderNorm.indexOf("/sent ") >= 0
                                                             || selectedFolderNorm.indexOf("/draft") >= 0
                        readonly property string mailboxForAvatar: showRecipient ? (recipient || "") : (sender || "")
                        readonly property string nameLabel: showRecipient
                                                      ? i18n("To: %1", root.appRoot.displayRecipientNames(recipient, accountEmail))
                                                      : root.appRoot.displaySenderName(sender, accountEmail)
                        height: Kirigami.Units.gridUnit * 3 + Kirigami.Units.smallSpacing + 14
                        radius: 0
                        clip: true
                        readonly property bool isChecked: !!(root.appRoot.selectedMessageKeys
                                                            && root.appRoot.selectedMessageKeys[messageKeyValue])
                        color: isChecked
                               ? Qt.lighter(root.systemPalette.highlight, 1.35)
                               : root.appRoot.selectedMessageKey === messageKeyValue
                                 ? Qt.lighter(root.systemPalette.highlight, 1.18) : "transparent"
                        border.color: "transparent"

                        MouseArea {
                            id: messageMouseArea
                            anchors.fill: parent
                            hoverEnabled: true

                            onEntered: {
                                const payload = "messageKey=" + (messageCard.messageKeyValue || "") + "\n"
                                              + "accountEmail=" + (accountEmail || "") + "\n"
                                              + "folder=" + (folder || "") + "\n"
                                              + "uid=" + (uid || "") + "\n"
                                              + "receivedAt=" + (receivedAt || "") + "\n"
                                              + "sender=" + (sender || "") + "\n"
                                              + "subject=" + (subject || "")
                                const p = messageMouseArea.mapToItem(null, mouseX, mouseY)
                                groupedMessageList.showDebugHoverPopup(payload, p.x, p.y)
                            }

                            onExited: groupedMessageList.hideDebugHoverPopup()

                            onClicked: function(mouse) {
                                if (mouse.modifiers & Qt.ShiftModifier
                                        && root.appRoot.lastClickedMessageIndex >= 0
                                        && root.appRoot.messageListModelObj) {
                                    // Shift+click: range-select all message rows between anchor and here
                                    const from = Math.min(index, root.appRoot.lastClickedMessageIndex)
                                    const to   = Math.max(index, root.appRoot.lastClickedMessageIndex)
                                    const next = Object.assign({}, root.appRoot.selectedMessageKeys)
                                    const mdl  = root.appRoot.messageListModelObj
                                    for (let i = from; i <= to; ++i) {
                                        const row = mdl.rowAt(i)
                                        if (row && !row.isHeader && row.messageKey)
                                            next[row.messageKey] = true
                                    }
                                    root.appRoot.selectedMessageKeys = next
                                } else if (mouse.modifiers & Qt.ControlModifier) {
                                    // Ctrl+click: toggle in multiselect set without changing content view
                                    root.appRoot.lastClickedMessageIndex = index
                                    const next = Object.assign({}, root.appRoot.selectedMessageKeys)
                                    if (next[messageCard.messageKeyValue])
                                        delete next[messageCard.messageKeyValue]
                                    else
                                        next[messageCard.messageKeyValue] = true
                                    root.appRoot.selectedMessageKeys = next
                                } else {
                                    // Normal click: open message, clear multiselect
                                    root.appRoot.lastClickedMessageIndex = index
                                    root.appRoot.selectedMessageKeys = ({})
                                    console.log("[qml-click]", "key=", messageCard.messageKeyValue,
                                                "account=", accountEmail, "folder=", folder, "uid=", uid,
                                                "index=", index)
                                    root.appRoot.selectedMessageKey = messageCard.messageKeyValue
                                    if (root.appRoot.imapServiceObj && root.appRoot.imapServiceObj.hydrateMessageBody) {
                                        root.appRoot.imapServiceObj.hydrateMessageBody(accountEmail, folder, uid)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            ColumnLayout {
                                Layout.preferredWidth: 18
                                Layout.fillHeight: true
                                spacing: 4

                                Item { Layout.fillHeight: true }

                                // Unread dot
                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    width: 9
                                    height: 9
                                    radius: 4.5
                                    color: root.systemPalette.highlight
                                    opacity: !!unread ? 1 : 0.25
                                    visible: !!unread
                                }

                                // Flag icon on hover
                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    visible: messageMouseArea.containsMouse

                                    Kirigami.Icon {
                                        anchors.fill: parent
                                        source: "flag-symbolic"
                                        opacity: 0.85
                                    }
                                }

                                Item { Layout.fillHeight: true }
                            }

                            Item {
                                id: avatarWrap
                                width: Kirigami.Units.iconSizes.medium + 4
                                height: Kirigami.Units.iconSizes.medium + 4

                                property var avatarSources: root.appRoot.senderAvatarSources(
                                                               messageCard.mailboxForAvatar,
                                                               "",
                                                               "",
                                                               accountEmail)

                                AvatarBadge {
                                    anchors.fill: parent
                                    size: avatarWrap.width
                                    displayName: messageCard.showRecipient
                                                 ? root.appRoot.displayRecipientNames(recipient, accountEmail)
                                                 : root.appRoot.displaySenderName(sender, accountEmail)
                                    avatarSources: avatarWrap.avatarSources
                                }
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
                                        text: root.appRoot.formatListDate(receivedAt)
                                        opacity: 0.7
                                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                    }
                                }

                                QQC2.Label {
                                    text: (subject || i18n("(No subject)"))
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                    font.bold: !!unread
                                    font.pixelSize: 13
                                    color: Kirigami.Theme.textColor
                                    Layout.fillWidth: true
                                }

                                QQC2.Label { text: snippet || ""; opacity: 0.72; font.pixelSize: 12; color: Kirigami.Theme.textColor; visible: text.length > 0; elide: Text.ElideRight; wrapMode: Text.NoWrap; Layout.fillWidth: true }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 18
                                Layout.fillHeight: true
                                spacing: 4

                                Item { Layout.fillHeight: true }

                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    visible: messageMouseArea.containsMouse

                                    Kirigami.Icon {
                                        anchors.fill: parent
                                        source: "user-trash-symbolic"
                                        opacity: 0.85
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: function(mouse) {
                                            mouse.accepted = true
                                            root.appRoot.deleteSelectedMessages()
                                        }
                                    }
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }
            }
        }
    }
}
