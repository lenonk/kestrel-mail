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
            visible: (root.appRoot && root.appRoot.selectedFolderCategories && root.appRoot.selectedFolderCategories.length > 0)
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                id: gMailCategoryRepeater
                model: (root.appRoot && root.appRoot.selectedFolderCategories) ? root.appRoot.selectedFolderCategories : []
                delegate: MessageCategoryButton {
                    appRoot: root.appRoot
                    systemPalette: root.systemPalette
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
            property string pendingRestoreStableKey: ""
            property string lastFolderKey: (root.appRoot && root.appRoot.selectedFolderKey)
                                           ? root.appRoot.selectedFolderKey.toString()
                                           : ""
            property bool loadMoreQueued: false
            property bool windowShiftQueued: false

            Component.onCompleted: {
                if (root.appRoot && root.appRoot.messageListModelObj)
                    root.appRoot.messageListModelObj.setWindowSize(150)
            }

            function queueRestoreScroll() {
                if (!restorePending)
                    return
                scrollRestoreTimer.restart()
            }

            Timer {
                id: scrollRestoreTimer
                interval: 50
                repeat: false
                onTriggered: {
                    // Don't interrupt an active flick. For intentional navigation (folder
                    // switch, restoreTargetLocked) retry until the flick settles. For
                    // background model changes, just let the flick finish naturally.
                    if (groupedMessageList.flicking) {
                        if (groupedMessageList.restoreTargetLocked) {
                            scrollRestoreTimer.restart()
                        } else {
                            groupedMessageList.pendingRestoreIndex = -1
                            groupedMessageList.pendingRestoreStableKey = ""
                            groupedMessageList.restorePending = false
                        }
                        return
                    }

                    const stableKey = groupedMessageList.pendingRestoreStableKey
                    let resolved = -1

                    // Try stableKey lookup first — survives model resets where index shifts.
                    if (stableKey.length && root.appRoot.messageListModelObj) {
                        const n = groupedMessageList.count
                        for (let i = 0; i < n; ++i) {
                            const row = root.appRoot.messageListModelObj.rowAt(i)
                            if (row && row.messageKey && row.messageKey.toString() === stableKey) {
                                resolved = i
                                break
                            }
                        }
                    }

                    // Fall back to captured numeric index when stableKey is absent or not found.
                    if (resolved < 0)
                        resolved = groupedMessageList.pendingRestoreIndex

                    if (resolved >= 0 && resolved < groupedMessageList.count) {
                        groupedMessageList.positionViewAtIndex(resolved, ListView.Beginning)
                    } else {
                        const maxY = Math.max(0, groupedMessageList.contentHeight - groupedMessageList.height)
                        groupedMessageList.contentY = Math.max(0, Math.min(maxY, groupedMessageList.preservedContentY))
                    }
                    groupedMessageList.pendingRestoreIndex = -1
                    groupedMessageList.pendingRestoreStableKey = ""
                    groupedMessageList.restorePending = false
                    groupedMessageList.restoreTargetLocked = false
                }
            }

            // Waits for the flick to settle before shifting the visible window,
            // so the model reset from shiftWindowDown/Up never interrupts a flick.
            Timer {
                id: windowShiftTimer
                interval: 150
                repeat: false
                onTriggered: {
                    if (!root.appRoot.messageListModelObj) {
                        groupedMessageList.windowShiftQueued = false
                        return
                    }
                    if (groupedMessageList.flicking) {
                        windowShiftTimer.restart()
                        return
                    }
                    groupedMessageList.windowShiftQueued = false
                    const bottomGap = groupedMessageList.contentHeight
                                    - (groupedMessageList.contentY + groupedMessageList.height)
                    if (bottomGap < 600)
                        root.appRoot.messageListModelObj.shiftWindowDown()
                    else if (groupedMessageList.contentY < 600)
                        root.appRoot.messageListModelObj.shiftWindowUp()
                }
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
                    if (!groupedMessageList.restoreTargetLocked) {
                        groupedMessageList.preservedContentY = groupedMessageList.contentY
                        // Capture a stable anchor so positionViewAtIndex can restore the
                        // correct visual position even when rows shift during a model reset.
                        const topIdx = groupedMessageList.indexAt(1, groupedMessageList.contentY + 1)
                        groupedMessageList.pendingRestoreIndex = topIdx
                        // Also capture the stableKey (messageKey) of the top-visible row so
                        // we can locate it by identity after a reset where indices shift.
                        if (topIdx >= 0 && root.appRoot.messageListModelObj) {
                            const row = root.appRoot.messageListModelObj.rowAt(topIdx)
                            groupedMessageList.pendingRestoreStableKey = (row && row.messageKey)
                                ? row.messageKey.toString() : ""
                        } else {
                            groupedMessageList.pendingRestoreStableKey = ""
                        }
                    }
                    groupedMessageList.restorePending = true
                }

                function onModelReset() {
                    groupedMessageList.queueRestoreScroll()
                }

                function onRowsAboutToBeInserted(parent, first, last) {
                    // Pure bottom append (loadMore): rows are added after the current last
                    // item, so the viewport is completely unaffected — no restore needed.
                    if (first >= groupedMessageList.count)
                        return
                    if (!groupedMessageList.restoreTargetLocked) {
                        groupedMessageList.preservedContentY = groupedMessageList.contentY
                        // If rows are inserted before the current top-visible item, the item
                        // shifts down by the inserted count — compensate so the view stays put.
                        const topIdx = groupedMessageList.indexAt(1, groupedMessageList.contentY + 1)
                        if (topIdx >= 0 && first <= topIdx)
                            groupedMessageList.pendingRestoreIndex = topIdx + (last - first + 1)
                        else
                            groupedMessageList.pendingRestoreIndex = topIdx
                    }
                    groupedMessageList.restorePending = true
                }

                function onRowsAboutToBeRemoved(parent, first, last) {
                    if (!groupedMessageList.restoreTargetLocked) {
                        groupedMessageList.preservedContentY = groupedMessageList.contentY
                        const topIdx = groupedMessageList.indexAt(1, groupedMessageList.contentY + 1)
                        if (topIdx >= 0) {
                            if (topIdx >= first && topIdx <= last) {
                                // The top item itself is being removed — land at first survivor.
                                groupedMessageList.pendingRestoreIndex = first
                            } else if (first < topIdx) {
                                // Rows removed before the viewport — shift index back.
                                groupedMessageList.pendingRestoreIndex = topIdx - (last - first + 1)
                            } else {
                                groupedMessageList.pendingRestoreIndex = topIdx
                            }
                        } else {
                            groupedMessageList.pendingRestoreIndex = -1
                        }
                    }
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
                // Fetch more rows from DB when approaching the end of all loaded data.
                if (bottomGap < 1800 && !groupedMessageList.loadMoreQueued) {
                    groupedMessageList.loadMoreQueued = true
                    Qt.callLater(function() {
                        groupedMessageList.loadMoreQueued = false
                        if (root.appRoot.messageListModelObj)
                            root.appRoot.messageListModelObj.loadMore()
                    })
                }
                // Queue a window shift when near either edge of the visible window.
                // The timer defers the actual shift until the flick has settled.
                if (!groupedMessageList.windowShiftQueued
                        && (bottomGap < 600 || contentY < 600)) {
                    groupedMessageList.windowShiftQueued = true
                    windowShiftTimer.start()
                }
            }

            delegate: Item {
                id: rootDelegate
                width: groupedMessageList.width
                readonly property bool isHeaderRow: !!isHeader
                readonly property int topGap: 0
                readonly property string delegateKey: (typeof model !== "undefined" && typeof model.messageKey !== "undefined") ? model.messageKey.toString() : ""
                readonly property bool isPendingDelete: delegateKey.length > 0 && root.appRoot && root.appRoot.pendingDeleteKeys && !!root.appRoot.pendingDeleteKeys[delegateKey]
                visible: !isPendingDelete
                implicitHeight: isPendingDelete ? 0 : (topGap + (headerButton.visible ? (headerButton.implicitHeight + 10) : messageCard.height))

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
