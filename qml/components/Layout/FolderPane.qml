import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common
import "../Calendar" as Calendar

Rectangle {
    id: folderPane

    required property var appRoot
    required property var systemPalette

    color: Kirigami.Theme.backgroundColor

    onWidthChanged: {
        if (!appRoot.paneAutoToggleEnabled) return
        var collapseThreshold = appRoot.collapsedRailWidth + 2

        if (appRoot.folderPaneVisible) {
            if (width <= collapseThreshold) {
                appRoot.folderPaneHiddenByButton = false
                appRoot.folderPaneVisible = false
            } else {
                appRoot.folderPaneExpandedWidth = width
            }
        } else {
            if (width > collapseThreshold) {
                appRoot.folderPaneExpandedWidth = width
                appRoot.folderPaneVisible = true
            }
        }
    }

    // Expanded folder pane
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        visible: appRoot.folderPaneVisible
        spacing: 0

        PaneHeaderBar {
            title: appRoot.activeWorkspace === "calendar" ? i18n("Calendar") : i18n("Mail")
            titleBold: true
            titlePointSizeDelta: 1
            headerHeight: appRoot.folderHeaderHeight
            utilityIconSize: appRoot.headerUtilityIconSize
            utilityHighlightColor: folderPane.systemPalette.highlight
            collapseIconName: "go-previous-symbolic"
            collapseIconSize: 24
            collapseButtonSize: 44
            collapseRightMargin: -15
            onCollapseRequested: {
                appRoot.folderPaneHiddenByButton = true
                appRoot.folderPaneVisible = false
            }
        }

        FolderSectionButton {
            id: favoritesSectionBtn
            visible: appRoot.activeWorkspace === "mail"
            expanded: appRoot.favoritesExpanded
            sectionIcon: "favorite"
            title: i18n("Favorites")
            titleOpacity: 0.8
            rowHeight: appRoot.folderRowHeight
            chevronSize: appRoot.sectionChevronSize
            sectionIconSize: appRoot.folderListSectionIconSize
            onActivated: appRoot.favoritesExpanded = !appRoot.favoritesExpanded
            onContextMenuRequested: (x, y) => folderPane.favoritesMenuRequested(x, y)
        }

        Repeater {
            model: (appRoot.activeWorkspace === "mail" && appRoot.favoritesExpanded) ? appRoot.visibleFavoriteItems() : []
            delegate: FolderItemDelegate {
                readonly property var folderStats: appRoot.folderStatsByKey(modelData.key, "")
                rowHeight: appRoot.folderRowHeight
                iconSize: appRoot.folderListIconSize
                indentLevel: 1
                folderKey: modelData.key
                folderName: modelData.name
                folderIcon: modelData.icon
                unreadCount: folderStats.unread
                newMessageCount: folderStats.newMessages || 0
                selected: appRoot.selectedFolderKey === modelData.key
                tooltipText: appRoot.folderTooltipText(modelData.name, modelData.key, "")
                onActivated: appRoot.selectedFolderKey = modelData.key
            }
        }

        FolderSectionButton {
            visible: appRoot.activeWorkspace === "mail"
            Layout.topMargin: 6
            expanded: appRoot.tagsExpanded
            sectionIcon: "tag"
            title: i18n("Tags")
            titleOpacity: 0.8
            rowHeight: appRoot.folderRowHeight
            chevronSize: appRoot.sectionChevronSize
            sectionIconSize: appRoot.folderListSectionIconSize
            onActivated: appRoot.tagsExpanded = !appRoot.tagsExpanded
        }

        Repeater {
            model: (appRoot.activeWorkspace === "mail" && appRoot.tagsExpanded) ? appRoot.tagFolderItems() : []
            delegate: FolderItemDelegate {
                readonly property string rawFolderName: (modelData.rawName || modelData.name || "")
                readonly property var folderStats: appRoot.folderStatsByKey(modelData.key, rawFolderName)
                rowHeight: appRoot.folderRowHeight
                iconSize: appRoot.folderListIconSize
                indentLevel: 1
                folderKey: modelData.key
                folderName: modelData.name
                folderIcon: modelData.icon
                iconColor: (modelData.accentColor || "transparent")
                unreadCount: Number(modelData.unread || 0)
                newMessageCount: folderStats.newMessages || 0
                selected: appRoot.selectedFolderKey === modelData.key
                tooltipText: appRoot.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                acceptsDrop: true
                dropRawFolderName: rawFolderName
                onActivated: appRoot.selectedFolderKey = modelData.key
                onDropReceived: (targetFolder) => appRoot.copySelectedMessagesToFolder(targetFolder)
                onDragEnteredTarget: appRoot.messageDragOverTarget = true
                onDragExitedTarget: appRoot.messageDragOverTarget = false
            }
        }

        // ── Per-account folder sections ──────────────────────────────
        Repeater {
            model: (appRoot.activeWorkspace === "mail" && typeof accountManager !== "undefined")
                   ? accountManager.accounts : []
            delegate: ColumnLayout {
                required property int index
                required property var modelData
                readonly property var account: modelData
                readonly property string acctEmail: account ? account.email : ""
                readonly property bool acctExpanded: appRoot.accountExpandedState[acctEmail] !== false
                readonly property bool acctMoreExpanded: !!appRoot.moreExpandedState[acctEmail]
                Layout.fillWidth: true
                spacing: 0

                FolderSectionButton {
                    visible: true
                    Layout.topMargin: index === 0 ? 6 : 2
                    expanded: acctExpanded
                    sectionIcon: account ? account.avatarSource : "mail-message"
                    title: account ? account.accountName : ""
                    titleOpacity: 0.85
                    rowHeight: appRoot.folderRowHeight
                    chevronSize: appRoot.sectionChevronSize
                    sectionIconSize: appRoot.folderListSectionIconSize
                    rightActivityIcon: (account && account.syncing) ? "view-refresh" : ""
                    rightActivitySpinning: account ? account.syncing : false
                    rightStatusIcon: (account && account.needsReauth) ? "dialog-password"
                                   : (account && account.connected) ? "network-connect"
                                   : "network-disconnect"
                    rightThrottleIcon: (account && account.throttled) ? "dialog-warning" : ""
                    rightThrottleTooltip: (account && account.needsReauth)
                        ? i18n("Authentication expired. Click to re-authenticate.")
                        : (account && account.throttled)
                          ? i18n("Account is being throttled.")
                          : ""
                    onActivated: {
                        if (account && account.needsReauth) {
                            account.reauthenticate()
                        } else {
                            var s = Object.assign({}, appRoot.accountExpandedState)
                            s[acctEmail] = !acctExpanded
                            appRoot.accountExpandedState = s
                        }
                    }
                }

                Repeater {
                    model: acctExpanded ? appRoot.accountFolderItemsForEmail(acctEmail) : []
                    delegate: FolderItemDelegate {
                        readonly property string rawFolderName: (modelData.rawName || modelData.name || "")
                        readonly property var folderStats: appRoot.folderStatsByKey(modelData.key, rawFolderName)
                        rowHeight: appRoot.folderRowHeight
                        iconSize: appRoot.folderListIconSize
                        indentLevel: 1
                        folderKey: modelData.key
                        folderName: modelData.name
                        folderIcon: modelData.icon
                        unreadCount: folderStats.unread
                        newMessageCount: folderStats.newMessages || 0
                        selected: appRoot.selectedFolderKey === modelData.key
                        tooltipText: modelData.name + " - " + (folderStats.total || 0) + " items (" + (folderStats.unread || 0) + " unread)"
                        acceptsDrop: true
                        dropRawFolderName: rawFolderName
                        onActivated: appRoot.selectedFolderKey = modelData.key
                        onDropReceived: (targetFolder) => appRoot.moveSelectedMessagesToFolder(targetFolder)
                        onDragEnteredTarget: appRoot.messageDragOverTarget = true
                        onDragExitedTarget: appRoot.messageDragOverTarget = false
                    }
                }

                FolderItemDelegate {
                    visible: acctExpanded
                    rowHeight: appRoot.folderRowHeight
                    iconSize: appRoot.folderListIconSize
                    folderKey: ""
                    folderName: i18n("More")
                    folderIcon: "overflow-menu-horizontal"
                    indentLevel: 1
                    hasChildren: true
                    expanded: acctMoreExpanded
                    onToggleRequested: {
                        var s = Object.assign({}, appRoot.moreExpandedState)
                        s[acctEmail] = !acctMoreExpanded
                        appRoot.moreExpandedState = s
                    }
                }

                Repeater {
                    model: (acctExpanded && acctMoreExpanded) ? appRoot.moreAccountFolderItemsForEmail(acctEmail) : []
                    delegate: FolderItemDelegate {
                        readonly property string rawFolderName: (modelData.rawName || modelData.name || "")
                        readonly property var folderStats: appRoot.folderStatsByKey(modelData.key, rawFolderName)
                        rowHeight: appRoot.folderRowHeight
                        iconSize: appRoot.folderListIconSize
                        folderKey: modelData.key
                        folderName: modelData.name
                        folderIcon: modelData.icon
                        indentLevel: 2 + Number(modelData.level || 0)
                        hasChildren: !!modelData.hasChildren
                        expanded: !!modelData.expanded
                        unreadCount: modelData.noselect ? 0 : folderStats.unread
                        newMessageCount: modelData.noselect ? 0 : (folderStats.newMessages || 0)
                        selected: appRoot.selectedFolderKey === modelData.key
                        tooltipText: modelData.name + " - " + (folderStats.total || 0) + " items (" + (folderStats.unread || 0) + " unread)"
                        acceptsDrop: !modelData.noselect
                        dropRawFolderName: rawFolderName
                        onToggleRequested: appRoot.toggleMoreFolderExpanded(rawFolderName)
                        onActivated: {
                            if (!modelData.noselect) { appRoot.selectedFolderKey = modelData.key }
                        }
                        onDropReceived: (targetFolder) => appRoot.moveSelectedMessagesToFolder(targetFolder)
                        onDragEnteredTarget: appRoot.messageDragOverTarget = true
                        onDragExitedTarget: appRoot.messageDragOverTarget = false
                    }
                }
            }
        }

        FolderSectionButton {
            id: localFoldersSectionBtn
            visible: appRoot.activeWorkspace === "mail"
            Layout.topMargin: 6
            expanded: appRoot.localFoldersExpanded
            sectionIcon: "folder"
            title: i18n("Local Folders")
            titleOpacity: 0.85
            rowHeight: appRoot.folderRowHeight
            chevronSize: appRoot.sectionChevronSize
            sectionIconSize: appRoot.folderListSectionIconSize
            onActivated: appRoot.localFoldersExpanded = !appRoot.localFoldersExpanded
            onContextMenuRequested: (x, y) => folderPane.localFoldersMenuRequested(x, y)
        }

        Repeater {
            model: (appRoot.activeWorkspace === "mail" && appRoot.localFoldersExpanded) ? appRoot.allLocalFolderItems() : []
            delegate: FolderItemDelegate {
                readonly property var folderStats: appRoot.folderStatsByKey(modelData.key, "")
                rowHeight: appRoot.folderRowHeight
                iconSize: appRoot.folderListIconSize
                indentLevel: 1
                folderKey: modelData.key
                folderName: modelData.name
                folderIcon: modelData.icon
                unreadCount: folderStats.unread
                newMessageCount: folderStats.newMessages || 0
                selected: appRoot.selectedFolderKey === modelData.key
                tooltipText: appRoot.folderTooltipText(modelData.name, modelData.key, "")
                acceptsDrop: true
                dropRawFolderName: modelData.key
                onActivated: appRoot.selectedFolderKey = modelData.key
                onDropReceived: (targetFolder) => appRoot.copySelectedMessagesToLocalFolder(targetFolder)
                onDragEnteredTarget: appRoot.messageDragOverTarget = true
                onDragExitedTarget: appRoot.messageDragOverTarget = false
            }
        }

        Calendar.CalendarSidebarPane {
            Layout.fillWidth: true
            visibleInCalendar: appRoot.activeWorkspace === "calendar"
            gmailCalendarsExpanded: appRoot.gmailCalendarsExpanded
            calendarSources: appRoot.calendarSources
            accountIcon: appRoot.primaryAccountIcon
            onExpandedToggled: (expanded) => appRoot.gmailCalendarsExpanded = expanded
            onSourceToggled: (sourceId, checked) => appRoot.setCalendarSourceChecked(sourceId, checked)
        }

        Item { Layout.fillHeight: true }

        Common.PaneDivider {
            Layout.leftMargin: -Kirigami.Units.largeSpacing
            Layout.rightMargin: -Kirigami.Units.largeSpacing
        }
        PaneIconStrip {
            Layout.fillWidth: true
            vertical: false
            showLabel: false
            items: [
                { iconName: "mail-message", label: i18n("Mail"), toolTipText: i18n("Mail"), active: appRoot.activeWorkspace === "mail" },
                { iconName: "office-calendar", label: i18n("Calendar"), toolTipText: i18n("Calendar"), active: appRoot.activeWorkspace === "calendar" },
                { iconName: "user-identity", label: i18n("People"), toolTipText: i18n("People"), active: appRoot.activeWorkspace === "people" },
                { iconName: "overflow-menu-horizontal", label: i18n("More"), toolTipText: i18n("More"), useHorizontalDots: true }
            ]
            onItemClicked: function(_index, item) {
                var icon = (item && item.iconName) ? item.iconName : "";
                if (icon === "mail-message")
                    appRoot.activeWorkspace = "mail";
                else if (icon === "office-calendar")
                    appRoot.activeWorkspace = "calendar";
                else if (icon === "user-identity")
                    appRoot.activeWorkspace = "people";
            }
        }
    }

    // Collapsed folder rail
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Kirigami.Units.smallSpacing + 1
        anchors.bottomMargin: Kirigami.Units.smallSpacing + 2
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        visible: !appRoot.folderPaneVisible
        spacing: 0

        IconOnlyFlatButton {
            implicitWidth: parent.width
            implicitHeight: 44
            iconName: "go-next-symbolic"
            iconSize: 24
            onClicked: {
                var minExpanded = 190
                if (!appRoot.folderPaneHiddenByButton) {
                    appRoot.folderPaneExpandedWidth = Math.max(appRoot.folderPaneExpandedWidth, minExpanded)
                }
                appRoot.folderPaneVisible = true
            }
        }

        Repeater {
            model: [
                "mail-folder-inbox",
                "mail-mark-unread",
                "mail-mark-important",
                "internet-mail",
                "folder"
            ]
            delegate: IconOnlyFlatButton {
                implicitWidth: parent.width
                implicitHeight: 44
                iconName: modelData
                iconSize: 24
                topPadding: 0
                bottomPadding: 0
                leftInset: 0
                rightInset: 0
                topInset: 0
                bottomInset: 0
                clip: false
            }
        }

        Item { Layout.fillHeight: true }

        Common.PaneDivider {}

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: ""; showLabel: false; toolTipText: i18n("Mail"); active: appRoot.activeWorkspace === "mail"; hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4; onClicked: appRoot.activeWorkspace = "mail" }
            PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: ""; showLabel: false; toolTipText: i18n("Calendar"); active: appRoot.activeWorkspace === "calendar"; hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4; onClicked: appRoot.activeWorkspace = "calendar" }
            PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: ""; showLabel: false; toolTipText: i18n("People"); active: appRoot.activeWorkspace === "people"; hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4; onClicked: appRoot.activeWorkspace = "people" }
            PaneIconButton { Layout.fillWidth: true; iconName: "view-task"; label: ""; showLabel: false; toolTipText: i18n("Tasks"); hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
            PaneIconButton { Layout.fillWidth: true; iconName: "overflow-menu-horizontal"; label: ""; showLabel: false; hoverFeedback: true; toolTipText: i18n("More"); useHorizontalDots: true; underlineOnLeft: true; sideIndicatorInset: 4 }
        }
    }

    signal favoritesMenuRequested(real x, real y)
    signal localFoldersMenuRequested(real x, real y)
}
