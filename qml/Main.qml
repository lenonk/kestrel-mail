import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import Qt5Compat.GraphicalEffects
import QtCore
import org.kde.kirigami as Kirigami

import "components" as Components

Kirigami.ApplicationWindow {
    id: root
    width:  1600
    height: 1080
    visible: true
    title: i18n("Kestrel Mail")
    flags: Qt.Window | Qt.FramelessWindowHint

    color: Kirigami.Theme.backgroundColor

    SystemPalette { id: systemPalette }

    Item {
        id: shiftKeyTracker
        anchors.fill: parent
        z: -1
        focus: true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Shift) {
                if (!root.shiftKeyDown) {
                    root.shiftKeyDown = true
                    root.updateContentPaneHoverExpandState()
                }
            }
        }
        Keys.onReleased: (event) => {
            if (event.key === Qt.Key_Shift) {
                if (root.shiftKeyDown) {
                    root.shiftKeyDown = false
                    root.updateContentPaneHoverExpandState()
                }
            }
        }
        onActiveFocusChanged: {
            if (!activeFocus) forceActiveFocus()
        }
        Component.onCompleted: forceActiveFocus()
    }

    property string syncStatus: ""
    property bool syncStatusIsError: false
    property bool refreshInProgress: false
    property bool accountRefreshing: false
    property bool accountConnected: true
    property var inlineStatusQueue: []
    property int inlineStatusSeq: 0
    property bool folderPaneVisible: true
    property bool favoritesExpanded: true
    property bool tagsExpanded: true
    property bool accountExpanded: true
    property bool moreExpanded: false
    property var moreFolderExpandedState: ({})
    property bool localFoldersExpanded: false
    property real folderPaneExpandedWidth: 235
    property bool folderPaneHiddenByButton: false
    property bool rightPaneVisible: true
    property bool paneAutoToggleEnabled: false
    property real messageListPaneWidth: 470
    property real rightPaneExpandedWidth: 170
    property bool rightPaneHiddenByButton: false
    property bool contentPaneDarkModeEnabled: true
    property bool contentPaneHoverExpandEnabled: true
    property bool contentPaneHoverExpandActive: false
    property bool contentPaneHoverAreaHovered: false
    property bool shiftKeyDown: false
    property bool contentPaneHoverSnapshotValid: false
    property real contentPaneHoverMessageListWidth: 470
    property real contentPaneHoverPrevMessageListWidth: 470
    property real contentPaneHoverPrevRightPaneExpandedWidth: 170
    property bool contentPaneHoverPrevRightPaneVisible: true
    property int rightCollapsedRailWidth: 48
    property string selectedFolderKey: "account:inbox"
    property int selectedCategoryIndex: 0
    property bool categorySelectionExplicit: false
    property string selectedMessageKey: ""
    property var selectedMessageAnchor: ({})
    // Set of message keys currently checked in the message list (JS object used as Set).
    property var selectedMessageKeys: ({})
    property int lastClickedMessageIndex: -1
    property bool bootstrapFolderSyncRequested: false

    Settings {
        id: uiSettings
        category: "ui"
        property alias windowWidth: root.width
        property alias windowHeight: root.height
        property alias folderPaneExpandedWidth: root.folderPaneExpandedWidth
        property alias messageListPaneWidth: root.messageListPaneWidth
        property alias rightPaneExpandedWidth: root.rightPaneExpandedWidth
        property alias folderPaneVisible: root.folderPaneVisible
        property alias rightPaneVisible: root.rightPaneVisible
        property alias contentPaneDarkModeEnabled: root.contentPaneDarkModeEnabled
    }

    NumberAnimation {
        id: contentPaneHoverWidthAnim
        target: root
        property: "contentPaneHoverMessageListWidth"
        duration: 240
        easing.type: Easing.InOutCubic
    }

    Timer {
        id: markReadTimer
        interval: 2000
        repeat: false
        onTriggered: {
            const p = root.parseMessageKey(root.selectedMessageKey)
            if (!p || !root.imapServiceObj) return
            root.imapServiceObj.markMessageRead(p.accountEmail, p.folder, p.uid)
        }
    }

    // Keep list pane width updates immediate so splitters stay responsive.
    // rightPaneExpandedWidth should track SplitView drag immediately; no animation.

    property int windowEdgeGrabSize: 8
    property int minWindowWidth: 980
    property int minWindowHeight: 640

    readonly property var favoriteFolderItems: [
        { key: "favorites:all-inboxes", name: i18n("All Inboxes"), icon: "mail-folder-inbox", categories: [] },
        { key: "favorites:unread", name: i18n("Unread"), icon: "mail-mark-unread", categories: [] },
        { key: "favorites:flagged", name: i18n("Flagged"), icon: "mail-mark-important", categories: [] }
    ]

    readonly property var defaultAccountFolderItems: [
        { key: "account:inbox", name: i18n("Inbox"), icon: "mail-folder-inbox", categories: [i18n("Primary"), i18n("Promotions"), i18n("Social")] },
        { key: "account:sent", name: i18n("Sent"), icon: "mail-folder-sent", categories: [] },
        { key: "account:trash", name: i18n("Trash"), icon: "user-trash", categories: [] },
        { key: "account:drafts", name: i18n("Drafts"), icon: "document-edit", categories: [] },
        { key: "account:junk", name: i18n("Junk Email"), icon: "mail-mark-junk", categories: [] },
        { key: "account:all", name: i18n("All Mail"), icon: "folder", categories: [] }
    ]

    readonly property var localFolderItems: [
        { key: "local:inbox", name: i18n("Inbox"), icon: "mail-folder-inbox", categories: [] },
        { key: "local:outbox", name: i18n("Outbox"), icon: "mail-folder-outbox", categories: [] },
        { key: "local:sent", name: i18n("Sent"), icon: "mail-folder-sent", categories: [] },
        { key: "local:trash", name: i18n("Trash"), icon: "user-trash", categories: [] },
        { key: "local:drafts", name: i18n("Drafts"), icon: "document-edit", categories: [] },
        { key: "local:junk", name: i18n("Junk Email"), icon: "mail-mark-junk", categories: [] }
    ]

    function displayFolderName(rawName) {
        const name = (rawName || "").toString()
        let clean = name
        const idxBracket = name.lastIndexOf("]/")
        if (idxBracket >= 0 && idxBracket + 2 < name.length) clean = name.slice(idxBracket + 2)
        const idxSlash = clean.lastIndexOf("/")
        if (idxSlash >= 0 && idxSlash + 1 < clean.length) clean = clean.slice(idxSlash + 1)
        if (!clean.length) return name

        const lower = clean.toLowerCase()
        return lower.replace(/\b\w/g, function(c) { return c.toUpperCase() })
    }

    function isCategoryFolder(rawName) {
        const n = (rawName || "").toString().toLowerCase()
        return n.indexOf("/categories/") >= 0
    }

    function normalizedRemoteFolderName(rawName) {
        const n = (rawName || "").toString()
        if (n.toLowerCase().indexOf("[google mail]/") === 0) {
            return "[Gmail]/" + n.slice("[Google Mail]/".length)
        }
        return n
    }

    function isNoSelectFolder(flags) {
        const f = (flags || "").toString().toLowerCase()
        return f.indexOf("\\noselect") >= 0
    }

    function isSystemMailboxName(name, specialUse) {
        const n = (name || "").toString().toLowerCase()
        const s = (specialUse || "").toString().toLowerCase()
        if (s.length) return true
        if (n === "inbox" || n === "drafts" || n === "sent" || n === "sent mail" || n === "trash" || n === "junk" || n === "spam" || n === "all mail" || n === "starred" || n === "important") return true
        if (n.indexOf("[gmail]/") === 0) return true
        return false
    }

    function tagColorForName(name) {
        const colors = ["#F39C12", "#E74C3C", "#F1C40F", "#2ECC71", "#3498DB", "#9B59B6", "#1ABC9C", "#E67E22", "#8E44AD"]
        const s = (name || "").toString().toLowerCase()
        let h = 0
        for (let i = 0; i < s.length; ++i) h = ((h * 31) + s.charCodeAt(i)) & 0x7fffffff
        return colors[h % colors.length]
    }

    function isPrimaryAccountFolderNorm(norm) {
        const n = (norm || "").toString().toLowerCase()
        return n === "inbox" || n === "[gmail]/sent mail" || n === "[gmail]/trash" || n === "[gmail]/drafts" || n === "[gmail]/spam" || n === "[gmail]/all mail"
    }

    function iconForSpecialUse(specialUse, name) {
        const s = (specialUse || "").toLowerCase()
        const n = (name || "").toLowerCase()
        if (s === "inbox" || n === "inbox") return "mail-folder-inbox"
        if (s === "sent" || n.indexOf("sent") >= 0) return "mail-folder-sent"
        if (s === "trash" || n.indexOf("trash") >= 0 || n.indexOf("bin") >= 0) return "user-trash"
        if (s === "drafts" || n.indexOf("draft") >= 0) return "document-edit"
        if (s === "junk" || n.indexOf("junk") >= 0 || n.indexOf("spam") >= 0) return "mail-mark-junk"
        if (s === "all" || n.indexOf("all mail") >= 0) return "folder"
        return "folder"
    }

    function accountFolderItems() {
        const folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        if (!folders || folders.length === 0) return []

        const byNorm = {}
        for (let i = 0; i < folders.length; ++i) {
            const f = folders[i]
            const rawName = root.normalizedRemoteFolderName((f.name || "").toString())
            if (!rawName.length) continue
            if (root.isCategoryFolder(rawName)) continue
            if (root.isNoSelectFolder(f.flags)) continue
            if (rawName.indexOf("/") < 0 && !root.isSystemMailboxName(rawName, f.specialUse)) continue

            const norm = rawName.toLowerCase()
            if (byNorm[norm]) continue
            byNorm[norm] = {
                key: "account:" + norm,
                name: root.displayFolderName(rawName),
                rawName: rawName,
                icon: root.iconForSpecialUse(f.specialUse, rawName),
                categories: []
            }
        }

        const ordered = []
        const preferred = ["inbox", "[gmail]/sent mail", "[gmail]/trash", "[gmail]/drafts", "[gmail]/spam", "[gmail]/all mail"]
        for (let p = 0; p < preferred.length; ++p) {
            const k = preferred[p]
            if (byNorm[k]) {
                ordered.push(byNorm[k])
                delete byNorm[k]
            }
        }

        const rest = Object.keys(byNorm).sort()
        for (let r = 0; r < rest.length; ++r) {
            const item = byNorm[rest[r]]
            if (root.isPrimaryAccountFolderNorm((item.rawName || "").toString().toLowerCase())) ordered.push(item)
        }

        for (let j = 0; j < ordered.length; ++j) {
            if (ordered[j].name.toLowerCase() === "inbox") {
                const derived = root.dataStoreObj ? root.dataStoreObj.inboxCategoryTabs : ["Primary"]
                const cats = []
                for (let c = 0; c < derived.length; ++c) {
                    const key = (derived[c] || "").toString().toLowerCase()
                    if (key === "primary") cats.push(i18n("Primary"))
                    else if (key === "promotions") cats.push(i18n("Promotions"))
                    else if (key === "social") cats.push(i18n("Social"))
                }
                if (cats.length === 0) cats.push(i18n("Primary"))
                ordered[j].categories = cats
                break
            }
        }

        return ordered
    }

    function isMoreFolderExpanded(path) {
        const key = (path || "").toString().toLowerCase()
        if (!key.length) return true
        if (moreFolderExpandedState[key] === undefined) return true
        return !!moreFolderExpandedState[key]
    }

    function toggleMoreFolderExpanded(path) {
        const key = (path || "").toString().toLowerCase()
        if (!key.length) return
        const next = Object.assign({}, moreFolderExpandedState)
        next[key] = !isMoreFolderExpanded(key)
        moreFolderExpandedState = next
    }

    function moreAccountFolderItems() {
        const folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        if (!folders || folders.length === 0) return []

        const primaryKeys = {}
        const primary = root.accountFolderItems()
        for (let i = 0; i < primary.length; ++i) primaryKeys[(primary[i].rawName || "").toString().toLowerCase()] = true

        const byNorm = {}
        for (let i = 0; i < folders.length; ++i) {
            const f = folders[i]
            const rawName = root.normalizedRemoteFolderName((f.name || "").toString())
            if (!rawName.length) continue
            if (root.isCategoryFolder(rawName)) continue

            const norm = rawName.toLowerCase()
            if (primaryKeys[norm]) continue
            if (rawName.indexOf("/") < 0 && !root.isSystemMailboxName(rawName, f.specialUse)) continue

            const parts = rawName.split("/")
            let level = Math.max(0, parts.length - 1)
            if (parts.length > 1) {
                const head = (parts[0] || "").toLowerCase()
                if (head === "[gmail]" || head === "[google mail]") level = Math.max(0, level - 1)
            }

            byNorm[norm] = {
                key: "account:" + norm,
                name: root.displayFolderName(rawName),
                rawName: rawName,
                icon: root.iconForSpecialUse(f.specialUse, rawName),
                categories: [],
                flags: (f.flags || "").toString(),
                noselect: root.isNoSelectFolder(f.flags),
                level: level
            }
        }

        // Synthesize missing parent nodes from path delimiter hierarchy (e.g. Mailspring/Snoozed => parent Mailspring).
        const existing = Object.keys(byNorm)
        for (let i = 0; i < existing.length; ++i) {
            const raw = byNorm[existing[i]].rawName
            const parts = raw.split("/")
            if (parts.length < 2) continue

            let parentPath = parts[0]
            for (let p = 1; p < parts.length; ++p) {
                const parentNorm = parentPath.toLowerCase()
                if (!byNorm[parentNorm] && !primaryKeys[parentNorm]) {
                    byNorm[parentNorm] = {
                        key: "account:" + parentNorm,
                        name: root.displayFolderName(parentPath),
                        rawName: parentPath,
                        icon: "folder",
                        categories: [],
                        flags: "\\Noselect (synthetic)",
                        noselect: true,
                        level: Math.max(0, parentPath.split("/").length - 1)
                    }
                }
                parentPath += "/" + parts[p]
            }
        }

        const candidates = Object.keys(byNorm).map(function(k){ return byNorm[k] })
        candidates.sort(function(a, b) { return a.rawName.localeCompare(b.rawName) })

        const out = []
        for (let i = 0; i < candidates.length; ++i) {
            const c = candidates[i]
            const cNorm = c.rawName.toLowerCase()
            if (cNorm === "[gmail]" || cNorm === "[google mail]") continue
            const prefix = cNorm + "/"
            let hasChild = false
            for (let j = 0; j < candidates.length; ++j) {
                if (candidates[j].rawName.toLowerCase().indexOf(prefix) === 0) { hasChild = true; break }
            }
            if (c.noselect && !hasChild) continue

            const slash = c.rawName.lastIndexOf("/")
            const parentRaw = slash > 0 ? c.rawName.slice(0, slash) : ""

            // Show only when all ancestor parents are expanded.
            let visible = true
            let walk = parentRaw
            while (walk.length) {
                if (!root.isMoreFolderExpanded(walk)) { visible = false; break }
                const s = walk.lastIndexOf("/")
                walk = s > 0 ? walk.slice(0, s) : ""
            }
            if (!visible) continue

            out.push(Object.assign({}, c, {
                hasChildren: hasChild,
                expanded: root.isMoreFolderExpanded(c.rawName),
                parentRaw: parentRaw
            }))
        }

        return out
    }

    function tagFolderItems() {
        const tags = (root.dataStoreObj && root.dataStoreObj.tagItems) ? root.dataStoreObj.tagItems() : []
        const out = []
        for (let i = 0; i < tags.length; ++i) {
            const t = tags[i]
            const label = (t.label || "").toString()
            if (!label.length) continue
            out.push({
                key: "tag:" + label.toLowerCase(),
                name: root.displayFolderName((t.name || label).toString()),
                rawName: label,
                icon: "tag",
                categories: [],
                unread: Number(t.unread || 0),
                total: Number(t.total || 0),
                accentColor: (t.color || root.tagColorForName(label))
            })
        }
        out.sort(function(a, b) { return a.name.localeCompare(b.name) })
        return out
    }

    function folderEntryByKey(key) {
        const groups = [root.favoriteFolderItems, root.tagFolderItems(), root.accountFolderItems(), root.moreAccountFolderItems(), root.localFolderItems]
        for (let g = 0; g < groups.length; ++g) {
            const items = groups[g]
            for (let i = 0; i < items.length; ++i) {
                if (items[i].key === key) return items[i]
            }
        }
        return null
    }

    readonly property var selectedFolderEntry: root.folderEntryByKey(root.selectedFolderKey)
    readonly property var selectedFolderCategories: (root.selectedFolderEntry && root.selectedFolderEntry.categories)
                                                ? root.selectedFolderEntry.categories
                                                : []
    readonly property var selectedMessageData: root.messageByKey(root.selectedMessageKey)


    readonly property var mockInboxMessages: [
        { sender: "welcome@kestrel.mail", subject: "Welcome to Kestrel Mail", snippet: "Your account is set up. Press Refresh to load real mail from your provider.", receivedAt: "2026-02-17T11:00:00", unread: true }
    ]

    readonly property int titleBarHeight: Kirigami.Units.gridUnit   + 12
    readonly property int titleButtonHeight: Kirigami.Units.gridUnit  + 5
    readonly property int titleButtonWidth: Kirigami.Units.gridUnit  + 16
    readonly property int titleIconSize: Kirigami.Units.gridUnit    - 4
    readonly property int toolbarHeight: Kirigami.Units.gridUnit    + 25
    readonly property int titleSearchWidth: Kirigami.Units.gridUnit * 22

    readonly property int defaultRadius: Math.max(4, Kirigami.Units.smallSpacing)
    readonly property int panelMargin: Kirigami.Units.largeSpacing
    readonly property int sectionSpacing: Kirigami.Units.smallSpacing
    readonly property int folderRowHeight: Kirigami.Units.gridUnit + 8
    readonly property int folderHeaderHeight: Kirigami.Units.gridUnit + 22
    readonly property int collapsedRailWidth: 48
    readonly property int folderListIconSize: 20
    readonly property int folderListSectionIconSize: 24
    readonly property int sectionChevronSize: 13
    readonly property int headerUtilityIconSize: 20

    readonly property var accountSetupObj: (typeof accountSetup !== "undefined") ? accountSetup : null
    readonly property var accountRepositoryObj: (typeof accountRepository !== "undefined") ? accountRepository : null
    readonly property var providerProfilesObj: (typeof providerProfiles !== "undefined") ? providerProfiles : null
    readonly property var dataStoreObj: (typeof dataStore !== "undefined") ? dataStore : null
    readonly property var messageListModelObj: (typeof messageListModel !== "undefined") ? messageListModel : null
    readonly property var imapServiceObj: (typeof imapService !== "undefined") ? imapService : null
    readonly property string primaryAccountName: (root.accountRepositoryObj && root.accountRepositoryObj.accounts.length > 0 && root.accountRepositoryObj.accounts[0].accountName)
                                               ? root.accountRepositoryObj.accounts[0].accountName
                                               : i18n("Account")

    property string composeDraftTo: ""
    property string composeDraftSubject: ""
    property string composeDraftBody: ""

    Components.AccountWizardDialog {
        id: accountWizard
        accountSetupObj: root.accountSetupObj
        accountRepositoryObj: root.accountRepositoryObj
        onToastRequested: (message, isError) => root.showInlineStatus(message, isError)
    }

    QQC2.Dialog {
        id: composeDialog
        title: i18n("Compose")
        modal: true
        width: Math.min(root.width * 0.72, 900)
        height: Math.min(root.height * 0.72, 640)
        standardButtons: QQC2.Dialog.Close

        contentItem: ColumnLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("To")
                text: root.composeDraftTo
                onTextChanged: root.composeDraftTo = text
            }

            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("Subject")
                text: root.composeDraftSubject
                onTextChanged: root.composeDraftSubject = text
            }

            QQC2.TextArea {
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: i18n("Message")
                text: root.composeDraftBody
                wrapMode: TextEdit.Wrap
                onTextChanged: root.composeDraftBody = text
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                QQC2.Button {
                    text: i18n("Send")
                    enabled: root.composeDraftTo.trim().length > 0
                    onClicked: {
                        root.showInlineStatus(i18n("Send requested to %1").arg(root.composeDraftTo), false)
                        composeDialog.close()
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        root.width = Math.max(root.minWindowWidth, root.width)
        root.height = Math.max(root.minWindowHeight, root.height)
        root.folderPaneExpandedWidth = Math.max(root.collapsedRailWidth, root.folderPaneExpandedWidth)
        root.messageListPaneWidth = Math.max(320, root.messageListPaneWidth)
        root.contentPaneHoverMessageListWidth = root.messageListPaneWidth
        root.rightPaneExpandedWidth = Math.max(root.rightCollapsedRailWidth, root.rightPaneExpandedWidth)

        root.syncMessageListModelSelection()
        if (root.messageListModelObj && root.messageListModelObj.setExpansionState) {
            root.messageListModelObj.setExpansionState(root.todayExpanded,
                                                       root.yesterdayExpanded,
                                                       root.lastWeekExpanded,
                                                       root.twoWeeksAgoExpanded,
                                                       root.olderExpanded)
        }
        if (root.accountRepositoryObj && root.accountRepositoryObj.accounts.length === 0) {
            accountWizard.open()
        }
        Qt.callLater(function() {
            root.paneAutoToggleEnabled = true
            root.bootstrapSyncIfNeeded()
            if (root.imapServiceObj && root.imapServiceObj.initialize) {
                root.imapServiceObj.initialize()
            }
        })
    }

    Connections {
        target: root.imapServiceObj
        function onSyncFinished(ok, message) {
            root.refreshInProgress = false
            root.accountRefreshing = false
            root.accountConnected = !!ok
            root.showInlineStatus(message, !ok)
            if (ok && root.hasFetchedFolders() && root.bootstrapFolderSyncRequested) {
                root.bootstrapFolderSyncRequested = false
                root.syncSelectedFolder(false)
            } else if (!ok && !root.hasFetchedFolders()) {
                // allow retrying bootstrap discovery on next folder action
                root.bootstrapFolderSyncRequested = false
            }
        }

        function onHydrateStatus(ok, message) {
            root.showInlineStatus(message, !ok)
        }

        function onRealtimeStatus(ok, message) {
            root.accountConnected = !!ok
            root.showInlineStatus(message, !ok)
        }

        function onSyncActivityChanged(active) {
            root.accountRefreshing = !!active
            root.refreshInProgress = !!active
        }
    }

    Connections {
        target: root.dataStoreObj
        ignoreUnknownSignals: true
        function onFoldersChanged() {
            root.syncMessageListModelSelection()
            if (root.bootstrapFolderSyncRequested && root.hasFetchedFolders()) {
                root.bootstrapFolderSyncRequested = false
                // First start/account creation: seed recent messages for all discovered folders.
                if (root.imapServiceObj) {
                    root.accountRefreshing = true
                    root.refreshInProgress = true
                    root.imapServiceObj.syncAll(false)
                }
            }
        }
        function onInboxChanged() {
            // Keep current selection stable; messageByKey() now resolves against DB
            // when the row is outside the in-memory inbox cache window.
        }
    }

    Connections {
        target: root.accountRepositoryObj
        ignoreUnknownSignals: true
        function onAccountsChanged() {
            // New/updated account (e.g., OAuth just completed + save) should trigger discovery + fetch immediately.
            root.bootstrapFolderSyncRequested = false
            const hadFolders = root.hasFetchedFolders()
            root.bootstrapSyncIfNeeded()

            if (root.imapServiceObj) {
                root.refreshInProgress = true
                root.accountRefreshing = true
            }

            // Avoid duplicate fetch passes: bootstrap path (folders-only -> onFoldersChanged -> syncSelectedFolder)
            // handles first-run; when folders already exist, do one direct selected-folder sync.
            if (hadFolders) {
                root.syncSelectedFolder(true, true)
            }
        }
    }

    onSelectedFolderKeyChanged: {
        root.selectedCategoryIndex = 0
        root.categorySelectionExplicit = (root.selectedFolderCategories && root.selectedFolderCategories.length > 0)
        root.lastClickedMessageIndex = -1
        root.syncMessageListModelSelection()
    }
    onSelectedCategoryIndexChanged: {
        root.syncMessageListModelSelection()
    }
    onSelectedMessageKeyChanged: {
        markReadTimer.stop()
        if (root.selectedMessageKey.length > 0)
            markReadTimer.restart()
        if (!root.selectedMessageData) {
            root.setContentPaneHoverExpanded(false)
            return
        }
        root.selectedMessageAnchor = {
            accountEmail: root.selectedMessageData.accountEmail || "",
            sender: root.selectedMessageData.sender || "",
            subject: root.selectedMessageData.subject || "",
            receivedAt: root.selectedMessageData.receivedAt || ""
        }
        const hasUsableHtml = root.isBodyHtmlUsable(root.selectedMessageData.bodyHtml)
        if (!hasUsableHtml) {
            root.setContentPaneHoverExpanded(false)
            root.requestHydrateForMessageKey(root.selectedMessageKey)
        }
    }
    onMessageListPaneWidthChanged: {
        if (!root.contentPaneHoverExpandActive) {
            root.contentPaneHoverMessageListWidth = root.messageListPaneWidth
        }
    }
    onContentPaneHoverExpandEnabledChanged: {
        if (!root.contentPaneHoverExpandEnabled) {
            root.setContentPaneHoverExpanded(false)
        }
    }

    onSelectedFolderCategoriesChanged: {
        if (root.selectedFolderKey === "account:inbox" && root.selectedFolderCategories.length > 0 && !root.categorySelectionExplicit) {
            root.categorySelectionExplicit = true
            root.selectedCategoryIndex = Math.min(root.selectedCategoryIndex, root.selectedFolderCategories.length - 1)
            root.syncMessageListModelSelection()
        }
    }
    property bool todayExpanded: true
    property bool yesterdayExpanded: true
    property bool lastWeekExpanded: true
    property bool twoWeeksAgoExpanded: true
    property bool olderExpanded: true
    onTodayExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("today", root.todayExpanded)
    onYesterdayExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("yesterday", root.yesterdayExpanded)
    onLastWeekExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("lastWeek", root.lastWeekExpanded)
    onTwoWeeksAgoExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("twoWeeksAgo", root.twoWeeksAgoExpanded)
    onOlderExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("older", root.olderExpanded)

    function bucketKeyForDate(dateValue) {
        const d = new Date(dateValue)
        if (isNaN(d.getTime())) return "older"

        const now = new Date()
        const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const targetStart = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        const dayMs = 24 * 60 * 60 * 1000
        const diffDays = Math.floor((todayStart.getTime() - targetStart.getTime()) / dayMs)

        if (diffDays <= 0) return "today"
        if (diffDays === 1) return "yesterday"

        const weekStart = new Date(todayStart)
        weekStart.setDate(todayStart.getDate() - todayStart.getDay()) // Sunday-start week
        if (targetStart >= weekStart && targetStart < todayStart) {
            const dow = targetStart.getDay() // 0..6
            return "weekday-" + (dow === 0 ? 7 : dow)
        }

        if (diffDays <= 14) return "lastWeek"
        if (diffDays <= 21) return "twoWeeksAgo"
        return "older"
    }

    function _displayNameFromMailbox(rawValue, fallbackText) {
        let s = (rawValue || "").toString().trim()
        if (!s.length) return fallbackText
        // If multiple recipients are present, use the first mailbox for compact UI labels.
        const comma = s.indexOf(",")
        if (comma > 0) s = s.slice(0, comma).trim()
        const lt = s.indexOf("<")
        if (lt > 0) s = s.slice(0, lt).trim()
        s = s.replace(/<[^>]*>/g, " ")
        s = s.replace(/(^|\s)[A-Za-z]+>/g, " ")
        s = s.replace(/&[A-Za-z0-9#]+;/g, " ")
        s = s.replace(/["']/g, "")
        s = s.replace(/\s+/g, " ").trim()
        if (!s.length) {
            const email = root.senderEmail(rawValue)
            if (email.length) s = email.split("@")[0]
        }
        // If remaining token is still a full email, collapse to local-part for display.
        if (/^[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}$/i.test(s)) {
            s = s.split("@")[0]
        }
        return s.length ? s : fallbackText
    }

    function resolvedMailboxEmail(rawValue, accountEmailHint) {
        const raw = (rawValue || "").toString().trim()
        const parsed = root.senderEmail(raw)
        if (parsed.length) return parsed

        const token = raw.toLowerCase()
        const account = (accountEmailHint || "").toString().trim().toLowerCase()
        const at = account.indexOf("@")
        if (at > 0 && token.length && token === account.slice(0, at)) {
            return account
        }
        return ""
    }

    function displayNameForAddress(rawAddressValue, accountEmailHint) {
        const raw = (rawAddressValue || "").toString().trim()
        const email = root.resolvedMailboxEmail(raw, accountEmailHint)
        if (email.length) {
            if (root.dataStoreObj && root.dataStoreObj.displayNameForEmail) {
                const known = (root.dataStoreObj.displayNameForEmail(email) || "").toString().trim()
                if (known.length) return known
            }
            return email
        }
        return ""
    }

    function displaySenderName(senderValue, accountEmailHint) {
        const resolved = root.displayNameForAddress(senderValue, accountEmailHint)
        if (resolved.length) return resolved
        const raw = (senderValue || "").toString().trim()
        return raw.length ? raw : i18n("Unknown sender")
    }

    function _titleCaseWords(s) {
        const raw = (s || "").toString().trim()
        if (!raw.length) return ""

        // Only normalize when the name is effectively ALL CAPS.
        const hasLower = /[a-z]/.test(raw)
        const hasUpper = /[A-Z]/.test(raw)
        if (!(hasUpper && !hasLower)) {
            return raw.replace(/\s+/g, " ").trim()
        }

        const parts = raw.toLowerCase().split(/\s+/)
        return parts.map(function(p) { return p.length ? (p.charAt(0).toUpperCase() + p.slice(1)) : "" }).join(" ").trim()
    }

    function _accountDisplayNameFromEmail(email) {
        const e = (email || "").toString().trim().toLowerCase()
        const at = e.indexOf("@")
        if (at <= 0) return ""
        const local = e.slice(0, at).replace(/[._-]+/g, " ").trim()
        // Account-derived fallback should always be humanized title case.
        const parts = local.split(/\s+/)
        return parts.map(function(p) { return p.length ? (p.charAt(0).toUpperCase() + p.slice(1)) : "" }).join(" ").trim()
    }

    function displayRecipientName(recipientValue, accountEmailHint) {
        const resolved = root.displayNameForAddress(recipientValue, accountEmailHint)
        if (resolved.length) return resolved
        const raw = (recipientValue || "").toString().trim()
        return raw.length ? raw : i18n("Recipient")
    }

    function _splitRecipientMailboxes(recipientValue) {
        const raw = (recipientValue || "").toString().trim()
        if (!raw.length) return []
        // Split by commas not enclosed in double quotes.
        const parts = raw.split(/,(?=(?:[^"]*"[^"]*")*[^"]*$)/)
        return parts.map(function(p) { return (p || "").trim() }).filter(function(p) { return p.length > 0 })
    }

    function displayRecipientNames(recipientValue, accountEmailHint) {
        const mailboxes = root._splitRecipientMailboxes(recipientValue)
        if (!mailboxes.length) return root.displayRecipientName(recipientValue, accountEmailHint)
        const labels = []
        for (let i = 0; i < mailboxes.length; ++i) {
            const label = root.displayRecipientName(mailboxes[i], accountEmailHint)
            if (label.length) labels.push(label)
        }
        return labels.length ? labels.join(", ") : root.displayRecipientName(recipientValue, accountEmailHint)
    }

    function formatListDate(dateValue) {
        const d = new Date(dateValue)
        if (isNaN(d.getTime())) return ""
        const bucket = root.bucketKeyForDate(dateValue)
        if (bucket === "today") return d.toLocaleString(Qt.locale(), "h:mm AP")
        if (bucket === "yesterday" || bucket.startsWith("weekday-") || bucket === "lastWeek") return d.toLocaleString(Qt.locale(), "ddd h:mm AP")
        return d.toLocaleString(Qt.locale(), "ddd M/d")
    }

    function avatarInitials(nameValue) {
        const s = (nameValue || "").toString().trim()
        if (!s.length) return "?"
        const parts = s.split(/\s+/).filter(function(p) { return p.length > 0 })
        if (parts.length >= 2) {
            return (parts[0].charAt(0) + parts[1].charAt(0)).toUpperCase()
        }
        return parts[0].charAt(0).toUpperCase()
    }

    function emphasizeSubjectEmoji(subjectValue) {
        const s = (subjectValue || "").toString()
        const escaped = s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        return escaped.replace(/([\uD800-\uDBFF][\uDC00-\uDFFF])/g, '<span style="font-size:20px;">$1</span>')
    }

    function subjectRichText(subjectValue) {
        const s = (subjectValue || i18n("(No subject)")).toString()
        const escaped = s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        return escaped.replace(/[\uD800-\uDBFF][\uDC00-\uDFFF]|[\u2600-\u27BF]/g,
                               function(m) { return "<span style='font-size:18px'>" + m + "</span>" })
    }

    function senderEmail(senderValue) {
        const s = (senderValue || "").toString()
        const m = s.match(/([A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,})/i)
        return m && m[1] ? m[1].toLowerCase() : ""
    }

    function senderDomain(senderValue) {
        const s = (senderValue || "").toString()
        const m = s.match(/[A-Z0-9._%+-]+@([A-Z0-9.-]+\.[A-Z]{2,})/i)
        if (!m || !m[1]) return ""
        const full = m[1].toLowerCase()
        const parts = full.split('.')
        if (parts.length <= 2) return full

        const tail2 = parts.slice(-2).join('.')
        const cc2 = ["co.uk", "com.au", "co.jp", "com.br", "com.mx"]
        const tail3 = parts.slice(-3).join('.')
        return cc2.indexOf(tail2) >= 0 ? tail3 : tail2
    }

    function senderAvatarSources(senderValue, avatarDomainHint, avatarUrlHint, accountEmailHint) {
        const email = root.senderEmail(senderValue)
        const accountEmail = (accountEmailHint || (root.selectedMessageData && root.selectedMessageData.accountEmail)
                              || "").toString().trim().toLowerCase()
        const selfIdentity = email.length && accountEmail.length && email === accountEmail
        if (selfIdentity) return []

        const identityCached = (root.dataStoreObj && root.dataStoreObj.avatarForEmail && email.length)
                ? (root.dataStoreObj.avatarForEmail(email) || "")
                : ""

        const normalized = identityCached.toString().trim()
        if (!normalized.length)
            return []

        const lower = normalized.toLowerCase()
        const isHttpUrl = lower.startsWith("https://") || lower.startsWith("http://")
        const isDataImage = lower.startsWith("data:image/")
        const isSupportedDataImage = isDataImage && (
            lower.startsWith("data:image/png")
            || lower.startsWith("data:image/jpeg")
            || lower.startsWith("data:image/jpg")
            || lower.startsWith("data:image/gif")
            || lower.startsWith("data:image/webp")
            || lower.startsWith("data:image/svg+xml")
        )

        if (!(isHttpUrl || isSupportedDataImage))
            return []

        const senderDomainValue = email.indexOf("@") >= 0 ? email.split("@")[1].toLowerCase() : ""
        const isGoogleProfileish = lower.indexOf("googleusercontent.com") >= 0
                                 || lower.indexOf("people.googleapis.com") >= 0
                                 || lower.indexOf("gstatic.com") >= 0
                                 || lower.indexOf("ggpht.com") >= 0

        const isGoogleS2Favicon = lower.indexOf("google.com/s2/favicons") >= 0
                                   && (senderDomainValue === "gmail.com" || senderDomainValue === "googlemail.com")

        // Guardrails:
        // - suppress google profile/default avatar surfaces that may resolve to placeholders
        // - suppress Google S2 favicon URLs (often tiny/placeholder or visually blank)
        if (isGoogleProfileish || isGoogleS2Favicon)
            return []

        return [normalized]
    }

    function avatarSourceLabel(urlValue) {
        const u = (urlValue || "").toString().toLowerCase()
        if (!u.length) return "initials"
        if (u.indexOf("people.googleapis.com") >= 0 || u.indexOf("googleusercontent.com") >= 0) return "google-people"
        if (u.indexOf("_bimi") >= 0 || u.indexOf("/bimi/") >= 0 || u.indexOf("vmc.") >= 0) return "bimi"
        if (u.indexOf("google.com/s2/favicons") >= 0) return "favicon"
        if (u.indexOf("gravatar.com/avatar") >= 0) return "gravatar"
        return "custom"
    }

    function parseMessageKey(key) {
        const s = (key || "").toString()
        const prefix = "msg:"
        if (!s.startsWith(prefix)) return null
        const parts = s.slice(prefix.length).split("|")
        if (parts.length < 3) return null
        return { accountEmail: parts[0], folder: parts[1], uid: parts[2] }
    }

    function isBodyHtmlUsable(value) {
        const html = (value || "").toString().trim()
        if (!html.length) return false
        const lower = html.toLowerCase()
        if (lower.indexOf("ok success [throttled]") >= 0) return false
        if (lower.indexOf("authenticationfailed") >= 0) return false
        if (lower.indexOf("no-fetch-payload") >= 0) return false
        const hasHtmlish = /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html)
        return hasHtmlish
    }

    function messageByKey(key) {
        const p = root.parseMessageKey(key)
        if (!p || !root.dataStoreObj || !root.dataStoreObj.inbox) return null
        const rows = root.dataStoreObj.inbox

        let base = null
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i]
            if ((r.accountEmail || "") === p.accountEmail
                    && (r.folder || "") === p.folder
                    && (r.uid || "") === p.uid) {
                base = r
                break
            }
        }

        // Keep content pane stable when UID/folder changes for the same logical message.
        if (!base && root.selectedMessageAnchor
                && (root.selectedMessageAnchor.accountEmail || "") === (p.accountEmail || "")) {
            for (let i = 0; i < rows.length; ++i) {
                const r = rows[i]
                if ((r.accountEmail || "") !== (root.selectedMessageAnchor.accountEmail || "")) continue
                if ((r.sender || "") !== (root.selectedMessageAnchor.sender || "")) continue
                if ((r.subject || "") !== (root.selectedMessageAnchor.subject || "")) continue
                if ((r.receivedAt || "") !== (root.selectedMessageAnchor.receivedAt || "")) continue
                base = r
                break
            }
        }

        let dbRow = null
        if (root.dataStoreObj && root.dataStoreObj.messageByKey) {
            dbRow = root.dataStoreObj.messageByKey(p.accountEmail, p.folder, p.uid)
            if (dbRow) {
                base = dbRow
                console.log("[qml-messageByKey-db-authoritative]", key, p.accountEmail, p.folder, p.uid)
            } else if (!base) {
                base = dbRow
                console.log("[qml-messageByKey-db-fallback]", key, p.accountEmail, p.folder, p.uid, !!base)
            }
        }

        if (!base) {
            console.log("[qml-messageByKey-miss]", key, p.accountEmail, p.folder, p.uid)
            return null
        }

        // If DB has fresher hydrated body for this exact key, prefer it over stale inbox cache row.
        if (dbRow && root.isBodyHtmlUsable(dbRow.bodyHtml) && !root.isBodyHtmlUsable(base.bodyHtml)) {
            base = dbRow
            console.log("[qml-messageByKey-db-prefer-body]", key)
        }

        return base
    }

    function formatContentDate(dateValue) {
        const d = new Date(dateValue)
        if (isNaN(d.getTime())) return ""
        const bucket = root.bucketKeyForDate(dateValue)
        if (bucket === "today") return d.toLocaleString(Qt.locale(), "h:mm AP")
        return d.toLocaleString(Qt.locale(), "ddd M/d/yyyy h:mm AP")
    }

    function requestHydrateForMessageKey(key) {
        const p = root.parseMessageKey(key)
        if (!p || !root.imapServiceObj || !root.imapServiceObj.hydrateMessageBody) return
        const row = root.messageByKey(key)
        const hasUsableHtml = row ? root.isBodyHtmlUsable(row.bodyHtml) : false
        if (!hasUsableHtml) {
            console.log("[qml] hydrate-request", key, p.accountEmail, p.folder, p.uid)
            root.imapServiceObj.hydrateMessageBody(p.accountEmail, p.folder, p.uid)
        }
    }

    function canHoverExpandContentPane() {
        if (!root.contentPaneHoverExpandEnabled) return false
        if (!root.selectedMessageData) return false
        return root.isBodyHtmlUsable(root.selectedMessageData.bodyHtml)
    }

    function updateContentPaneHoverExpandState() {
        const shouldExpand = root.contentPaneHoverAreaHovered && root.shiftKeyDown
        root.setContentPaneHoverExpanded(shouldExpand)
    }

    function setContentPaneHoverExpanded(active) {
        if (active) {
            if (!root.canHoverExpandContentPane()) return
            if (root.contentPaneHoverExpandActive) return
            root.contentPaneHoverExpandActive = true
            root.contentPaneHoverSnapshotValid = true
            root.contentPaneHoverPrevMessageListWidth = root.messageListPaneWidth
            root.contentPaneHoverPrevRightPaneExpandedWidth = root.rightPaneExpandedWidth
            root.contentPaneHoverPrevRightPaneVisible = root.rightPaneVisible

            contentPaneHoverWidthAnim.stop()
            contentPaneHoverWidthAnim.from = root.contentPaneHoverMessageListWidth
            contentPaneHoverWidthAnim.to = 0
            contentPaneHoverWidthAnim.start()
            if (root.rightPaneVisible) {
                root.rightPaneVisible = false
            }
        } else {
            if (!root.contentPaneHoverExpandActive) return
            root.contentPaneHoverExpandActive = false
            if (!root.contentPaneHoverSnapshotValid) return

            const restoreWidth = Math.max(320, root.contentPaneHoverPrevMessageListWidth)
            contentPaneHoverWidthAnim.stop()
            contentPaneHoverWidthAnim.from = root.contentPaneHoverMessageListWidth
            contentPaneHoverWidthAnim.to = restoreWidth
            contentPaneHoverWidthAnim.start()
            root.messageListPaneWidth = restoreWidth
            root.rightPaneExpandedWidth = Math.max(root.rightCollapsedRailWidth, root.contentPaneHoverPrevRightPaneExpandedWidth)
            root.rightPaneVisible = root.contentPaneHoverPrevRightPaneVisible
            root.contentPaneHoverSnapshotValid = false
        }
    }

    function showInlineStatus(message, isError) {
        const text = (message || "").toString().trim()
        if (!text.length) return

        root.syncStatus = text
        root.syncStatusIsError = !!isError

        const id = ++root.inlineStatusSeq
        const next = root.inlineStatusQueue.slice()
        next.push({ id: id, text: text, isError: !!isError })
        while (next.length > 3) {
            next.shift()
        }
        root.inlineStatusQueue = next
    }

    function dismissInlineStatus(statusId) {
        const next = []
        for (let i = 0; i < root.inlineStatusQueue.length; ++i) {
            const item = root.inlineStatusQueue[i]
            if (item.id !== statusId) next.push(item)
        }
        root.inlineStatusQueue = next
    }

    function openComposeDialog(contextLabel) {
        root.showInlineStatus(i18n("Compose opened (%1)").arg(contextLabel || i18n("message view")), false)
        composeDialog.open()
    }

    function openComposerTo(address, contextLabel) {
        const email = (address || "").toString().trim()
        if (!email.length) {
            openComposeDialog(contextLabel)
            return
        }
        root.composeDraftTo = email
        if (!root.composeDraftSubject.length && root.selectedMessageData && root.selectedMessageData.subject) {
            root.composeDraftSubject = i18n("Re: %1").arg(root.selectedMessageData.subject)
        }
        root.showInlineStatus(i18n("Compose opened for %1 (%2)").arg(email).arg(contextLabel || i18n("from message view")), false)
        composeDialog.open()
    }

    // Returns the raw IMAP folder name for Trash on the given account (host).
    // Falls back to "Trash" for non-Gmail servers.
    function trashFolderForHost(imapHost) {
        const h = (imapHost || "").toLowerCase()
        if (h.indexOf("gmail") >= 0 || h.indexOf("googlemail") >= 0)
            return "[Gmail]/Trash"
        return "Trash"
    }

    // Move a single message to the given IMAP target folder and update UI state.
    function moveMessageToFolder(accountEmail, folder, uid, targetFolder) {
        if (!imapServiceObj) return
        // Deselect if this was the viewed message
        if (root.selectedMessageKey === "msg:" + accountEmail + "|" + folder + "|" + uid)
            root.selectedMessageKey = ""
        // Uncheck from multi-select set
        const next = Object.assign({}, root.selectedMessageKeys)
        delete next["msg:" + accountEmail + "|" + folder + "|" + uid]
        root.selectedMessageKeys = next
        imapServiceObj.moveMessage(accountEmail, folder, uid, targetFolder)
    }

    // Delete all checked messages (or the currently viewed one if nothing is checked).
    // "Delete" means move to Trash.
    function deleteSelectedMessages() {
        if (!imapServiceObj) return
        const keys = Object.keys(root.selectedMessageKeys)
        if (keys.length > 0) {
            // Batch delete all checked messages
            const accs = root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
            for (const key of keys) {
                const p = root.parseMessageKey(key)
                if (!p) continue
                let host = ""
                for (const a of accs) {
                    const acc = typeof a.toMap === "function" ? a.toMap() : a
                    if ((acc.email || acc["email"] || "") === p.accountEmail) {
                        host = acc.imapHost || acc["imapHost"] || ""
                        break
                    }
                }
                moveMessageToFolder(p.accountEmail, p.folder, p.uid, trashFolderForHost(host))
            }
            root.selectedMessageKeys = ({})
            root.selectedMessageKey = ""
        } else if (root.selectedMessageData) {
            // Single message — current viewed message
            const d = root.selectedMessageData
            const accs = root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
            let host = ""
            for (const a of accs) {
                const acc = typeof a.toMap === "function" ? a.toMap() : a
                if ((acc.email || acc["email"] || "") === d.accountEmail) {
                    host = acc.imapHost || acc["imapHost"] || ""
                    break
                }
            }
            moveMessageToFolder(d.accountEmail, d.folder, d.uid, trashFolderForHost(host))
        }
    }

    function setBucketExpanded(bucketKey, expanded) {
        if (bucketKey === "today") root.todayExpanded = expanded
        else if (bucketKey === "yesterday") root.yesterdayExpanded = expanded
        else if (bucketKey === "lastWeek") root.lastWeekExpanded = expanded
        else if (bucketKey === "twoWeeksAgo") root.twoWeeksAgoExpanded = expanded
        else root.olderExpanded = expanded
        if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) {
            root.messageListModelObj.setBucketExpanded(bucketKey, expanded)
        }
    }

    function normalizedFolderFromKey(key) {
        if (!key || key.indexOf("account:") !== 0) return ""
        return key.slice("account:".length).toLowerCase()
    }

    function selectedImapFolderName() {
        if (root.selectedFolderKey.indexOf("account:") !== 0) return ""
        const target = root.normalizedFolderFromKey(root.selectedFolderKey)
        const folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        for (let i = 0; i < folders.length; ++i) {
            const raw = (folders[i].name || "").toString()
            if (raw.toLowerCase() === target) {
                // Category tabs should not switch IMAP SELECT target; keep syncing INBOX
                // and let backend classify rows into category folders.
                return raw
            }
        }
        return target.length ? target : ""
    }

    function hasFetchedFolders() {
        const folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        return !!folders && folders.length > 0
    }

    function bootstrapSyncIfNeeded() {
        if (!root.imapServiceObj || root.bootstrapFolderSyncRequested) return
        if (root.hasFetchedFolders()) return
        root.bootstrapFolderSyncRequested = true
        root.accountRefreshing = true
        // First-run bootstrap: discover folders/categories only (no message fetch).
        root.imapServiceObj.refreshFolderList()
    }

    function syncSelectedFolder(forceFetch, silent) {
        if (!root.imapServiceObj) return
        if (!root.hasFetchedFolders()) {
            root.bootstrapSyncIfNeeded()
            return
        }
        // Folder/category clicks should only change visible rows, not trigger IMAP fetch.
        if (!forceFetch) return

        const folder = root.selectedImapFolderName()
        if (!folder.length) return
        root.accountRefreshing = true
        root.imapServiceObj.syncFolder(folder, !silent)
    }

    function syncMessageListModelSelection() {
        if (!root.messageListModelObj || !root.messageListModelObj.setSelection) return
        const cats = root.categorySelectionExplicit ? (root.selectedFolderCategories || []) : []
        root.messageListModelObj.setSelection(root.selectedFolderKey || "",
                                              cats,
                                              root.selectedCategoryIndex)
    }

    function folderStatsByKey(folderKey, rawFolderName) {
        if (!root.dataStoreObj || !root.dataStoreObj.statsForFolder) return ({ total: 0, unread: 0 })
        // touch inbox so bindings update when message data changes
        const _inboxDep = root.dataStoreObj.inbox
        return root.dataStoreObj.statsForFolder(folderKey || "", rawFolderName || "")
    }

    function folderTooltipText(displayName, folderKey, rawFolderName) {
        const s = root.folderStatsByKey(folderKey, rawFolderName)
        return displayName + " - " + s.total + " items (" + s.unread + " unread)"
    }

    Timer {
        id: backgroundRefreshTimer
        interval: 120000
        running: true
        repeat: true
        onTriggered: {
            if (!root.visible || !root.imapServiceObj) return
            root.syncSelectedFolder(false, true)
        }
    }

    // Resize handles for frameless window
    MouseArea { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.RightEdge) }
    MouseArea { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 3; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.TopEdge) }
    MouseArea { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 3; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.BottomEdge) }

    MouseArea { anchors.left: parent.left; anchors.top: parent.top; width: 3; height: 3; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.TopEdge | Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.top: parent.top; width: 3; height: 3; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.TopEdge | Qt.RightEdge) }
    MouseArea { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 3; height: 3; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 3; height: 3; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.titleBarHeight
            Layout.topMargin: 0
            color: Kirigami.Theme.backgroundColor

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onPressed: root.startSystemMove()
            }

            Item {
                anchors.fill: parent

                Components.TitleBarIconButton {
                    id: menuButton
                    anchors.left: parent.left
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    buttonWidth: root.titleButtonWidth
                    buttonHeight: root.titleButtonHeight
                    iconSize: root.titleIconSize
                    highlightColor: systemPalette.highlight
                    iconName: "open-menu-symbolic"
                    onClicked: appMenu.openIfClosed()
                }

                QQC2.Label {
                    anchors.left: menuButton.right
                    anchors.leftMargin: Kirigami.Units.smallSpacing + 2
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18n("Kestrel")
                    font.bold: true
                    font.pointSize: 14 // 18
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 5
                    width: root.titleSearchWidth
                    height: Kirigami.Units.gridUnit + 10
                    radius: height / 2
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.06)
                    border.color: Kirigami.Theme.disabledTextColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.smallSpacing + 1
                        anchors.rightMargin: Kirigami.Units.smallSpacing + 1
                        spacing: 2

                        Kirigami.Icon {
                            source: "edit-find"
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            Layout.alignment: Qt.AlignVCenter
                            color: Kirigami.Theme.disabledTextColor
                        }
                        QQC2.TextField {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            placeholderText: i18n("Search (type '?' for help)")
                            font.pixelSize: 12
                            font.italic: true
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0
                            implicitHeight: parent.height - 2
                            background: Item {}
                        }
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Components.TitleBarIconButton {
                        id: minBtn
                        buttonWidth: root.titleButtonWidth
                        buttonHeight: root.titleButtonHeight
                        iconSize: root.titleIconSize
                        highlightColor: systemPalette.highlight
                        iconName: "window-minimize-symbolic"
                        onClicked: root.showMinimized()
                    }

                    Components.TitleBarIconButton {
                        id: maxBtn
                        buttonWidth: root.titleButtonWidth
                        buttonHeight: root.titleButtonHeight
                        iconSize: root.titleIconSize
                        highlightColor: systemPalette.highlight
                        iconName: root.visibility === Window.Maximized ? "window-restore-symbolic" : "window-maximize-symbolic"
                        onClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                    }

                    Components.TitleBarIconButton {
                        id: closeBtn
                        buttonWidth: root.titleButtonWidth
                        buttonHeight: root.titleButtonHeight
                        iconSize: root.titleIconSize
                        highlightColor: systemPalette.highlight
                        iconName: "window-close-symbolic"
                        onClicked: Qt.quit()
                    }
                }
            }

            Components.PopupMenu {
                id: appMenu
                parent: menuButton
                QQC2.MenuItem { text: i18n("Add Account..."); onTriggered: accountWizard.open() }
                QQC2.MenuSeparator {}
                QQC2.MenuItem { text: i18n("Settings") }
                QQC2.MenuItem { text: i18n("Accounts") }
                QQC2.MenuSeparator {}
                QQC2.MenuItem { text: i18n("Exit"); onTriggered: Qt.quit() }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.toolbarHeight
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

                    Components.MailActionButton {
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
                    }
                    Components.MailActionButton {
                        iconName: "view-refresh"
                        text: i18n("Refresh")
                        spinning: root.refreshInProgress
                        onTriggered: {
                            if (!root.imapServiceObj) return
                            root.refreshInProgress = true
                            root.accountRefreshing = true
                            root.imapServiceObj.syncAll()
                        }
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Kirigami.Units.largeSpacing

                    Components.MailActionButton { iconName: "mail-reply-sender"; text: i18n("Reply"); menuItems: [{ text: i18n("Reply"), icon: "mail-reply-sender" }, { text: i18n("Reply to all"), icon: "mail-reply-all" }] }
                    Components.MailActionButton { iconName: "mail-reply-all"; text: i18n("Reply All") }
                    Components.MailActionButton { iconName: "mail-forward"; text: i18n("Forward") }
                    Components.MailActionButton { iconName: "mail-mark-important"; text: i18n("Mark"); menuItems: [{ text: i18n("Read"), icon: "mail-mark-read" }, { text: i18n("Unread"), icon: "mail-mark-unread" }] }
                    Components.MailActionButton { iconName: "archive-insert"; text: i18n("Archive") }
                    Components.MailActionButton { iconName: "edit-delete"; text: i18n("Delete"); onTriggered: root.deleteSelectedMessages() }
                }
            }
        }

        QQC2.SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            // Keep built-in SplitView handle inert; we draw our own divider lines for visuals.
            handle: Rectangle {
                implicitWidth: 0
                implicitHeight: 1
                color: "transparent"
            }

            Rectangle {
                id: leftPaneContainer
                QQC2.SplitView.minimumWidth: root.collapsedRailWidth
                QQC2.SplitView.preferredWidth: root.folderPaneVisible ? root.folderPaneExpandedWidth : root.collapsedRailWidth
                QQC2.SplitView.fillHeight: true
                color: Kirigami.Theme.backgroundColor

                onWidthChanged: {
                    if (!root.paneAutoToggleEnabled) return
                    const collapseThreshold = root.collapsedRailWidth + 2

                    if (root.folderPaneVisible) {
                        if (width <= collapseThreshold) {
                            root.folderPaneHiddenByButton = false
                            root.folderPaneVisible = false
                        } else {
                            root.folderPaneExpandedWidth = width
                        }
                    } else {
                        if (width > collapseThreshold) {
                            root.folderPaneExpandedWidth = width
                            root.folderPaneVisible = true
                        }
                    }
                }

                // Expanded folder pane
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    visible: root.folderPaneVisible
                    spacing: 0

                    Components.PaneHeaderBar {
                        title: i18n("Mail")
                        titleBold: true
                        titlePointSizeDelta: 1
                        headerHeight: root.folderHeaderHeight
                        utilityIconSize: root.headerUtilityIconSize
                        utilityHighlightColor: systemPalette.highlight
                        collapseIconName: "go-previous-symbolic"
                        collapseIconSize: 24
                        collapseButtonSize: 44
                        collapseRightMargin: -15
                        onCollapseRequested: {
                            root.folderPaneHiddenByButton = true
                            root.folderPaneVisible = false
                        }
                    }

                    Components.FolderSectionButton {
                        expanded: root.favoritesExpanded
                        sectionIcon: "favorite"
                        title: i18n("Favorites")
                        titleOpacity: 0.8
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        onActivated: root.favoritesExpanded = !root.favoritesExpanded
                    }

                    Repeater {
                        model: root.favoritesExpanded ? root.favoriteFolderItems : []
                        delegate: Components.FolderItemDelegate {
                            property var folderStats: root.folderStatsByKey(modelData.key, "")
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            unreadCount: folderStats.unread
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, "")
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Components.FolderSectionButton {
                        expanded: root.tagsExpanded
                        sectionIcon: "tag"
                        title: i18n("Tags")
                        titleOpacity: 0.8
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        onActivated: root.tagsExpanded = !root.tagsExpanded
                    }

                    Repeater {
                        model: root.tagsExpanded ? root.tagFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            property string rawFolderName: (modelData.rawName || modelData.name || "")
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            iconColor: (modelData.accentColor || "transparent")
                            unreadCount: Number(modelData.unread || 0)
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Components.FolderSectionButton {
                        expanded: root.accountExpanded
                        sectionIcon: "internet-mail"
                        title: root.primaryAccountName
                        titleOpacity: 0.85
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        rightActivityIcon: root.accountRefreshing ? "view-refresh" : ""
                        rightActivitySpinning: root.accountRefreshing
                        rightStatusIcon: root.accountConnected ? "network-connect" : "network-disconnect"
                        onActivated: root.accountExpanded = !root.accountExpanded
                    }

                    Repeater {
                        model: root.accountExpanded ? root.accountFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            property string rawFolderName: (modelData.rawName || modelData.name || "")
                            property var folderStats: root.folderStatsByKey(modelData.key, rawFolderName)
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            unreadCount: folderStats.unread
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Components.FolderSectionButton {
                        visible: root.accountExpanded
                        expanded: root.moreExpanded
                        sectionIcon: "overflow-menu-horizontal"
                        title: i18n("More")
                        titleOpacity: 0.8
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        onActivated: root.moreExpanded = !root.moreExpanded
                    }

                    Repeater {
                        model: (root.accountExpanded && root.moreExpanded) ? root.moreAccountFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            property string rawFolderName: (modelData.rawName || modelData.name || "")
                            property var folderStats: root.folderStatsByKey(modelData.key, rawFolderName)
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            indentLevel: 1 + Number(modelData.level || 0)
                            hasChildren: !!modelData.hasChildren
                            expanded: !!modelData.expanded
                            unreadCount: modelData.noselect ? 0 : folderStats.unread
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                            onToggleRequested: root.toggleMoreFolderExpanded(rawFolderName)
                            onActivated: {
                                if (!modelData.noselect) root.selectedFolderKey = modelData.key
                            }
                        }
                    }

                    Components.FolderSectionButton {
                        expanded: root.localFoldersExpanded
                        sectionIcon: "folder"
                        title: i18n("Local Folders")
                        titleOpacity: 0.85
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        onActivated: root.localFoldersExpanded = !root.localFoldersExpanded
                    }

                    Repeater {
                        model: root.localFoldersExpanded ? root.localFolderItems : []
                        delegate: Components.FolderItemDelegate {
                            property var folderStats: root.folderStatsByKey(modelData.key, "")
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            unreadCount: folderStats.unread
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, "")
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Components.PaneDivider {
                        Layout.leftMargin: -Kirigami.Units.largeSpacing
                        Layout.rightMargin: -Kirigami.Units.largeSpacing
                    }
                    Components.PaneIconStrip {
                        Layout.fillWidth: true
                        vertical: false
                        showLabel: false
                        items: [
                            { iconName: "mail-message", label: i18n("Mail"), toolTipText: i18n("Mail"), active: true },
                            { iconName: "office-calendar", label: i18n("Calendar"), toolTipText: i18n("Calendar") },
                            { iconName: "user-identity", label: i18n("People"), toolTipText: i18n("People") },
                            { iconName: "overflow-menu-horizontal", label: i18n("More"), toolTipText: i18n("More"), useHorizontalDots: true }
                        ]
                    }
                }

                // Collapsed folder rail
                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: Kirigami.Units.smallSpacing + 1
                    anchors.bottomMargin: Kirigami.Units.smallSpacing + 2
                    anchors.leftMargin: 0
                    anchors.rightMargin: 0
                    visible: !root.folderPaneVisible
                    spacing: 0

                    Components.IconOnlyFlatButton {
                        implicitWidth: parent.width
                        implicitHeight: 44
                        iconName: "go-next-symbolic"
                        iconSize: 24
                        onClicked: {
                            const minExpanded = 190
                            if (!root.folderPaneHiddenByButton) {
                                root.folderPaneExpandedWidth = Math.max(root.folderPaneExpandedWidth, minExpanded)
                            }
                            root.folderPaneVisible = true
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
                        delegate: Components.IconOnlyFlatButton {
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

                    Components.PaneDivider {}

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: ""; showLabel: false; toolTipText: i18n("Mail"); active: true; hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: ""; showLabel: false; toolTipText: i18n("Calendar"); hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: ""; showLabel: false; toolTipText: i18n("People"); hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "view-task"; label: ""; showLabel: false; toolTipText: i18n("Tasks"); hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "overflow-menu-horizontal"; label: ""; showLabel: false; hoverFeedback: true; toolTipText: i18n("More"); useHorizontalDots: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                    }
                }
            }
            Components.MessageListPane {
                id: messageListPane
                opacity: root.contentPaneHoverExpandActive ? 0 : 1

                Behavior on opacity {
                    NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
                }

                QQC2.SplitView.minimumWidth: 0
                QQC2.SplitView.preferredWidth: root.contentPaneHoverMessageListWidth
                appRoot: root
                systemPalette: systemPalette
            }

            Components.MessageContentPane {
                id: messageContentPane
                QQC2.SplitView.minimumWidth: 420
                QQC2.SplitView.preferredWidth: 520
                QQC2.SplitView.fillWidth: true
                appRoot: root
                systemPalette: systemPalette

                HoverHandler {
                    enabled: root.canHoverExpandContentPane()
                    onHoveredChanged: {
                        root.contentPaneHoverAreaHovered = hovered
                        root.updateContentPaneHoverExpandState()
                    }
                }
            }
            Rectangle {
                id: rightPaneContainer
                QQC2.SplitView.minimumWidth: root.rightCollapsedRailWidth
                QQC2.SplitView.preferredWidth: root.rightPaneVisible ? root.rightPaneExpandedWidth : root.rightCollapsedRailWidth
                color: Kirigami.Theme.backgroundColor

                onWidthChanged: {
                    const collapseThreshold = root.rightCollapsedRailWidth + 2

                    // Always track live expanded width so SplitView drag stays responsive.
                    if (root.rightPaneVisible && width > collapseThreshold) {
                        root.rightPaneExpandedWidth = width
                    }

                    if (!root.paneAutoToggleEnabled) return

                    if (root.rightPaneVisible) {
                        if (width <= collapseThreshold) {
                            root.rightPaneHiddenByButton = false
                            root.rightPaneVisible = false
                        }
                    } else {
                        if (width > collapseThreshold) {
                            root.rightPaneExpandedWidth = width
                            root.rightPaneVisible = true
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.panelMargin
                    spacing: root.sectionSpacing
                    visible: root.rightPaneVisible

                    Components.PaneHeaderBar {
                        title: i18n("Invites")
                        titleBold: true
                        titlePointSizeDelta: 0
                        headerHeight: root.folderHeaderHeight
                        utilityIconSize: root.headerUtilityIconSize
                        utilityHighlightColor: systemPalette.highlight
                        collapseIconName: "go-next-symbolic"
                        collapseIconSize: 24
                        collapseButtonSize: 44
                        collapseRightMargin: -15
                        rowLeftMargin: 2
                        rowRightMargin: 2
                        onCollapseRequested: {
                            root.rightPaneHiddenByButton = true
                            root.rightPaneVisible = false
                        }
                    }

                    QQC2.Label { text: i18n("All Accounts") }

                    Item { Layout.preferredHeight: 4 }

                    Components.PaneDivider {
                        Layout.leftMargin: -root.panelMargin
                        Layout.rightMargin: -root.panelMargin
                    }

                    Repeater {
                        id: invitesRepeater
                        model: [i18n("Saturday, 4:30 PM"), i18n("Sunday, 3:30 PM")]
                        delegate: Components.InviteCard {
                            title: i18n("D&D")
                            whenText: modelData
                            fromText: i18n("from max@example.com")
                            sectionSpacing: root.sectionSpacing
                            radiusValue: root.defaultRadius
                            showBottomDivider: index < (invitesRepeater.count - 1)
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Components.PaneDivider {
                        Layout.leftMargin: -root.panelMargin
                        Layout.rightMargin: -root.panelMargin
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: i18n("People"); toolTipText: i18n("People"); showLabel: false; hoverFeedback: true }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "task-complete"; label: i18n("Tasks"); toolTipText: i18n("Tasks"); showLabel: false; hoverFeedback: true }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: i18n("Mail"); toolTipText: i18n("Mail"); showLabel: false; active: true; hoverFeedback: true }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: i18n("Calendar"); toolTipText: i18n("Calendar"); showLabel: false; hoverFeedback: true }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: Kirigami.Units.smallSpacing + 1
                    anchors.bottomMargin: Kirigami.Units.smallSpacing + 2
                    visible: !root.rightPaneVisible
                    spacing: 0

                    Components.IconOnlyFlatButton {
                        implicitWidth: parent.width
                        implicitHeight: 44
                        iconName: "go-previous-symbolic"
                        iconSize: 24
                        onClicked: {
                            const minExpanded = 150
                            if (!root.rightPaneHiddenByButton) {
                                root.rightPaneExpandedWidth = Math.max(root.rightPaneExpandedWidth, minExpanded)
                            }
                            root.rightPaneVisible = true
                        }
                    }

                    Repeater {
                        model: ["view-calendar-day", "mail-message", "user-identity", "task-complete"]
                        delegate: Components.IconOnlyFlatButton {
                            implicitWidth: parent.width
                            implicitHeight: 44
                            iconName: modelData
                            iconSize: 24
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Components.PaneDivider {}

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: ""; showLabel: false; toolTipText: i18n("People"); hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "task-complete"; label: ""; showLabel: false; toolTipText: i18n("Tasks"); hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: ""; showLabel: false; toolTipText: i18n("Mail"); active: true; hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: ""; showLabel: false; toolTipText: i18n("Calendar"); hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4 }
                    }
                }
            }

            Item {
                anchors.fill: parent
                z: 50

                // Visual splitter lines (non-interactive).
                Rectangle {
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    x: Math.round(messageListPane.x)
                }
                Rectangle {
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    x: Math.round(messageContentPane.x)
                }
                Rectangle {
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    x: Math.round(rightPaneContainer.x)
                }

                Rectangle {
                    color: "transparent"
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    x: messageListPane.x - width / 2
                    width: 8
                    property real startWidth: 0
                    HoverHandler { cursorShape: Qt.SizeHorCursor }
                    DragHandler {
                        target: null
                        onActiveChanged: {
                            if (active) parent.startWidth = root.folderPaneExpandedWidth
                        }
                        onTranslationChanged: {
                            if (!active) return
                            if (!root.folderPaneVisible && translation.x > 2) {
                                root.folderPaneVisible = true
                                root.folderPaneHiddenByButton = false
                                parent.startWidth = Math.max(root.folderPaneExpandedWidth, root.collapsedRailWidth + 24)
                            }
                            if (!root.folderPaneVisible) return
                            root.folderPaneExpandedWidth = Math.max(root.collapsedRailWidth, parent.startWidth + translation.x)
                        }
                    }
                }

                Rectangle {
                    color: "transparent"
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    // Keep drag hit-zone fully to the right of the divider so it doesn't cover
                    // the message-list scrollbar gutter.
                    x: messageContentPane.x
                    width: 4
                    property real startWidth: 0
                    HoverHandler { cursorShape: Qt.SizeHorCursor }
                    DragHandler {
                        target: null
                        onActiveChanged: {
                            if (active) parent.startWidth = root.messageListPaneWidth
                        }
                        onTranslationChanged: {
                            if (!active) return
                            root.messageListPaneWidth = Math.max(320, parent.startWidth + translation.x)
                        }
                    }
                }

                Rectangle {
                    color: "transparent"
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    x: rightPaneContainer.x - width / 2
                    width: 8
                    property real startWidth: 0
                    HoverHandler { cursorShape: Qt.SizeHorCursor }
                    DragHandler {
                        target: null
                        onActiveChanged: {
                            if (active) parent.startWidth = root.rightPaneExpandedWidth
                        }
                        onTranslationChanged: {
                            if (!active) return
                            if (!root.rightPaneVisible && translation.x < -2) {
                                root.rightPaneVisible = true
                                root.rightPaneHiddenByButton = false
                                parent.startWidth = Math.max(root.rightPaneExpandedWidth, root.rightCollapsedRailWidth + 24)
                            }
                            if (!root.rightPaneVisible) return
                            root.rightPaneExpandedWidth = Math.max(root.rightCollapsedRailWidth, parent.startWidth - translation.x)
                        }
                    }
                }
            }
        }
    }

    // Frameless-window resize grab handles.
    Item {
        anchors.fill: parent
        z: 10000

        // Corners first so diagonal resize works naturally.
        Rectangle {
            color: "transparent"
            anchors.left: parent.left
            anchors.top: parent.top
            width: root.windowEdgeGrabSize
            height: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeFDiagCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeFDiagCursor
                onPressed: root.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.right: parent.right
            anchors.top: parent.top
            width: root.windowEdgeGrabSize
            height: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeBDiagCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeBDiagCursor
                onPressed: root.startSystemResize(Qt.TopEdge | Qt.RightEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: root.windowEdgeGrabSize
            height: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeBDiagCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeBDiagCursor
                onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: root.windowEdgeGrabSize
            height: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeFDiagCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeFDiagCursor
                onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: root.windowEdgeGrabSize
            anchors.bottomMargin: root.windowEdgeGrabSize
            width: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeHorCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeHorCursor
                onPressed: root.startSystemResize(Qt.LeftEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: root.windowEdgeGrabSize
            anchors.bottomMargin: root.windowEdgeGrabSize
            width: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeHorCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeHorCursor
                onPressed: root.startSystemResize(Qt.RightEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: root.windowEdgeGrabSize
            anchors.rightMargin: root.windowEdgeGrabSize
            height: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeVerCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeVerCursor
                onPressed: root.startSystemResize(Qt.TopEdge)
            }
        }

        Rectangle {
            color: "transparent"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.windowEdgeGrabSize
            anchors.rightMargin: root.windowEdgeGrabSize
            height: root.windowEdgeGrabSize
            HoverHandler { cursorShape: Qt.SizeVerCursor }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeVerCursor
                onPressed: root.startSystemResize(Qt.BottomEdge)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        // border.color: Qt.darker(systemPalette.highlight, 1.2)
        border.color: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
        z: 9999
    }

}
