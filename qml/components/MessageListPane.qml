import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Messages"

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
            visible: (root.appRoot && root.appRoot.selectedFolderCategories && root.appRoot.selectedFolderCategories.length > 0)
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                id: gMailCategoryRepeater
                model: (root.appRoot && root.appRoot.selectedFolderCategories) ? root.appRoot.selectedFolderCategories : []
                delegate: MessageCategoryButton {
                    appRoot: root.appRoot
                    systemPalette: root.systemPalette
                    categoryName: String((typeof modelData !== "undefined") ? modelData : "")
                    categoryIndex: (typeof model !== "undefined" && typeof model.index !== "undefined") ? model.index : 0
                }
            }

            Item { Layout.fillWidth: true }
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
            property bool restoreTargetLocked: false
            property var folderScrollByKey: ({})
            property var folderTopIndexByKey: ({})
            property int pendingRestoreIndex: -1
            property string lastFolderKey: (root.appRoot && root.appRoot.selectedFolderKey)
                                           ? root.appRoot.selectedFolderKey.toString()
                                           : ""

            function queueRestoreScroll() {
                if (!restorePending)
                    return
                Qt.callLater(function() {
                    if (groupedMessageList.pendingRestoreIndex >= 0
                            && groupedMessageList.pendingRestoreIndex < groupedMessageList.count) {
                        groupedMessageList.positionViewAtIndex(groupedMessageList.pendingRestoreIndex, ListView.Beginning)
                    } else {
                        const maxY = Math.max(0, groupedMessageList.contentHeight - groupedMessageList.height)
                        groupedMessageList.contentY = Math.max(0, Math.min(maxY, groupedMessageList.preservedContentY))
                    }
                    groupedMessageList.pendingRestoreIndex = -1
                    groupedMessageList.restorePending = false
                    groupedMessageList.restoreTargetLocked = false
                })
            }

            Connections {
                target: root.appRoot
                ignoreUnknownSignals: true

                function onSelectedFolderKeyChanged() {
                    const oldKey = groupedMessageList.lastFolderKey
                    if (oldKey && oldKey.length) {
                        groupedMessageList.folderScrollByKey[oldKey] = groupedMessageList.contentY
                        groupedMessageList.folderTopIndexByKey[oldKey] = groupedMessageList.indexAt(0, 0)
                    }

                    const newKey = (root.appRoot.selectedFolderKey || "").toString()
                    groupedMessageList.lastFolderKey = newKey

                    const savedY = groupedMessageList.folderScrollByKey[newKey]
                    const targetY = (savedY === undefined || savedY === null) ? 0 : Number(savedY)
                    const savedIndex = groupedMessageList.folderTopIndexByKey[newKey]

                    groupedMessageList.preservedContentY = isFinite(targetY) ? targetY : 0
                    groupedMessageList.pendingRestoreIndex = (savedIndex === undefined || savedIndex === null)
                                                           ? -1 : Number(savedIndex)
                    groupedMessageList.restoreTargetLocked = true
                    groupedMessageList.restorePending = true
                    groupedMessageList.queueRestoreScroll()
                }
            }

            Connections {
                target: root.appRoot.messageListModelObj
                ignoreUnknownSignals: true

                function onModelAboutToBeReset() {
                    if (!groupedMessageList.restoreTargetLocked)
                        groupedMessageList.preservedContentY = groupedMessageList.contentY
                    groupedMessageList.restorePending = true
                }

                function onModelReset() {
                    groupedMessageList.queueRestoreScroll()
                }

                function onRowsAboutToBeInserted() {
                    if (!groupedMessageList.restoreTargetLocked)
                        groupedMessageList.preservedContentY = groupedMessageList.contentY
                    groupedMessageList.restorePending = true
                }

                function onRowsAboutToBeRemoved() {
                    if (!groupedMessageList.restoreTargetLocked)
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
                id: rootDelegate
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

                    MessageListHeader {
                        id: headerButton
                        isHeaderRow: rootDelegate.isHeaderRow
                        modelTitle: (typeof title !== "undefined") ? title : ""
                        modelExpanded: (typeof expanded !== "undefined") ? expanded : false
                        modelBucketKey: (typeof bucketKey !== "undefined") ? bucketKey : null
                        appRoot: root.appRoot
                    }

                    MessageCard {
                        id: messageCard
                        isHeaderRow: rootDelegate.isHeaderRow
                        appRoot: root.appRoot
                        systemPalette: root.systemPalette
                        groupedMessageList: groupedMessageList

                        modelMessageKey: (typeof model !== "undefined" && typeof model.messageKey !== "undefined") ? model.messageKey : ""
                        modelSender: (typeof model !== "undefined" && typeof model.sender !== "undefined") ? model.sender : ""
                        modelRecipient: (typeof model !== "undefined" && typeof model.recipient !== "undefined") ? model.recipient : ""
                        modelSubject: (typeof model !== "undefined" && typeof model.subject !== "undefined") ? model.subject : ""
                        modelUnread: (typeof model !== "undefined" && typeof model.unread !== "undefined") ? model.unread : false
                        modelThreadCount: (typeof model !== "undefined" && typeof model.threadCount !== "undefined") ? model.threadCount : 0
                        modelHasTrackingPixel: (typeof model !== "undefined" && typeof model.hasTrackingPixel !== "undefined") ? model.hasTrackingPixel : false
                        modelHasAttachments: (typeof model !== "undefined" && typeof model.hasAttachments !== "undefined") ? model.hasAttachments : false
                        modelIsImportant: (typeof model !== "undefined" && typeof model.isImportant !== "undefined") ? model.isImportant : false
                        modelSnippet: (typeof model !== "undefined" && typeof model.snippet !== "undefined") ? model.snippet : ""
                        modelAccountEmail: (typeof model !== "undefined" && typeof model.accountEmail !== "undefined") ? model.accountEmail : ""
                        modelFolder: (typeof model !== "undefined" && typeof model.folder !== "undefined") ? model.folder : ""
                        modelUid: (typeof model !== "undefined" && typeof model.uid !== "undefined") ? model.uid : ""
                        modelReceivedAt: (typeof model !== "undefined" && typeof model.receivedAt !== "undefined") ? model.receivedAt : ""
                        modelIndex: index
                    }
                }
            }
        }
    }
}
