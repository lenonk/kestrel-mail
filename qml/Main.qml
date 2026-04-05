import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import Qt5Compat.GraphicalEffects
import QtCore
import org.kde.kirigami as Kirigami

import "components/AccountWizard" as AccountWizard
import "components/Common" as Common
import "components/Layout" as AppLayout
import "components/Search" as Search
import "components/MessageList" as MessageList
import "components/MessageContent" as MessageContent
import "components/Compose" as Compose
import "components/Calendar" as Calendar
import "utils/ColorUtilities.js" as ColorUtils
import "utils/FolderUtils.js" as FolderUtils
import "utils/DisplayUtils.js" as DisplayUtils
import "utils/ComposeUtils.js" as ComposeUtils
import "utils/MailActions.js" as MailActions

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
            if (!activeFocus && !(titleBar.searchBar && titleBar.searchBar.editing))
                forceActiveFocus()
        }
        Component.onCompleted: forceActiveFocus()
    }

    signal webViewRefreshNeeded()
    Connections {
        target: typeof windowExposeWatcher !== "undefined" ? windowExposeWatcher : null
        function onWindowReExposed() { root.webViewRefreshNeeded() }
    }

    onVisibilityChanged: function(visibility) {
        if (visibility === Window.Windowed && root._needsAccountWizard) {
            root._needsAccountWizard = false
            _accountWizardTimer.start()
        }
    }

    Timer {
        id: _accountWizardTimer
        interval: 0
        repeat: false
        onTriggered: accountWizard.open()
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            if (titleBar.searchBar && titleBar.searchBar.enterEditing)
                titleBar.searchBar.enterEditing()
        }
    }

    // ── State properties ──

    property string syncStatus: ""
    property bool syncStatusIsError: false
    property bool refreshInProgress: false
    property string _lastToastMessage: ""
    property double _lastToastAtMs: 0
    property bool accountRefreshing: false
    property bool accountConnected: true
    property bool accountThrottled: false
    property string accountThrottleMessage: ""
    property bool accountNeedsReauth: false
    property string accountNeedsReauthEmail: ""
    property bool _needsAccountWizard: false
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
    property string activeWorkspace: "mail"
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
    property double lastMessageClickAtMs: 0
    property var selectedMessageAnchor: ({})
    property var selectedMessageKeys: ({})
    property var pendingDeleteKeys: ({})
    property int lastClickedMessageIndex: -1
    property bool bootstrapFolderSyncRequested: false

    property bool gmailCalendarsExpanded: true
    property var calendarSources: []
    property var calendarEvents: []

    property bool todayExpanded: true
    property bool yesterdayExpanded: true
    property bool lastWeekExpanded: true
    property bool twoWeeksAgoExpanded: true
    property bool olderExpanded: true

    // ── Reactivity versions ──

    property int _favoritesConfigRev: 0
    property int _userFoldersRev: 0
    property int _bodyUpdateVersion: 0

    Connections {
        target: root.dataStoreObj
        function onFavoritesConfigChanged() { root._favoritesConfigRev++ }
        function onUserFoldersChanged()     { root._userFoldersRev++ }
    }

    // ── Layout constants ──

    property int windowEdgeGrabSize: 8
    property int minWindowWidth: 980
    property int minWindowHeight: 640

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

    // ── Favorite / local folder definitions ──

    readonly property var allFavoriteDefinitions: [
        { key: "all-inboxes", name: i18n("All Inboxes"), icon: "mail-folder-inbox"   },
        { key: "unread",      name: i18n("Unread"),      icon: "mail-mark-unread"    },
        { key: "flagged",     name: i18n("Flagged"),     icon: "mail-mark-important" },
        { key: "outbox",      name: i18n("Outbox"),      icon: "mail-folder-outbox"  },
        { key: "sent",        name: i18n("Sent"),        icon: "mail-folder-sent"    },
        { key: "trash",       name: i18n("Trash"),       icon: "user-trash"          },
        { key: "drafts",      name: i18n("Drafts"),      icon: "document-edit"       },
        { key: "junk",        name: i18n("Junk Email"),  icon: "mail-mark-junk"      },
        { key: "archive",     name: i18n("Archive"),     icon: "folder"              },
        { key: "unreplied",   name: i18n("Unreplied"),   icon: "mail-reply-sender"   },
        { key: "snoozed",     name: i18n("Snoozed"),     icon: "appointment-soon"    }
    ]

    readonly property var defaultLocalFolderDefs: [
        { key: "local:inbox",  name: i18n("Inbox"),      icon: "mail-folder-inbox"  },
        { key: "local:outbox", name: i18n("Outbox"),     icon: "mail-folder-outbox" },
        { key: "local:sent",   name: i18n("Sent"),       icon: "mail-folder-sent"   },
        { key: "local:trash",  name: i18n("Trash"),      icon: "user-trash"         },
        { key: "local:drafts", name: i18n("Drafts"),     icon: "document-edit"      },
        { key: "local:junk",   name: i18n("Junk Email"), icon: "mail-mark-junk"     }
    ]

    // ── C++ object accessors ──

    readonly property var accountSetupObj: (typeof accountSetup !== "undefined") ? accountSetup : null
    readonly property var accountRepositoryObj: (typeof accountRepository !== "undefined") ? accountRepository : null
    readonly property var providerProfilesObj: (typeof providerProfiles !== "undefined") ? providerProfiles : null
    readonly property var dataStoreObj: (typeof dataStore !== "undefined") ? dataStore : null
    readonly property var messageListModelObj: (typeof messageListModel !== "undefined") ? messageListModel : null
    readonly property var imapServiceObj: (typeof imapService !== "undefined") ? imapService : null
    readonly property string primaryAccountName: (root.accountRepositoryObj && root.accountRepositoryObj.accounts.length > 0 && root.accountRepositoryObj.accounts[0].accountName)
                                               ? root.accountRepositoryObj.accounts[0].accountName
                                               : i18n("Account")
    readonly property string primaryAccountIcon: "qrc:/qml/images/gmail_account_icon.svg"

    // ── Derived properties ──

    readonly property var mockInboxMessages: [
        { sender: "welcome@kestrel.mail", subject: "Welcome to Kestrel Mail", snippet: "Your account is set up. Press Refresh to load real mail from your provider.", receivedAt: "2026-02-17T11:00:00", unread: true }
    ]

    readonly property var selectedFolderEntry: root.folderEntryByKey(root.selectedFolderKey)
    readonly property var selectedFolderCategories: (root.selectedFolderEntry && root.selectedFolderEntry.categories)
                                                ? root.selectedFolderEntry.categories
                                                : []
    readonly property var selectedMessageData: {
        void root._bodyUpdateVersion
        return root.messageByKey(root.selectedMessageKey)
    }

    // Expose messageContentPane id to child components (e.g. MailToolbar).
    readonly property alias messageContentPane: messageContentPane

    // ── Settings ──

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
            var p = DisplayUtils.parseMessageKey(root.selectedMessageKey)
            if (!p || !root.imapServiceObj) return
            root.imapServiceObj.markMessageRead(p.accountEmail, p.folder, p.uid)
        }
    }

    // ── Locale helper for .pragma library functions ──

    function _localeFmt(d, fmt) {
        return d.toLocaleString(Qt.locale(), fmt)
    }

    // ── Folder utility wrappers ──

    function visibleFavoriteItems() {
        void root._favoritesConfigRev
        var config = root.dataStoreObj ? root.dataStoreObj.favoritesConfig() : []
        return FolderUtils.visibleFavoriteItems(root.allFavoriteDefinitions, config)
    }

    function allLocalFolderItems() {
        void root._userFoldersRev
        var userFolders = root.dataStoreObj ? root.dataStoreObj.userFolders() : []
        return FolderUtils.allLocalFolderItems(root.defaultLocalFolderDefs, userFolders)
    }

    function displayFolderName(rawName) { return FolderUtils.displayFolderName(rawName) }
    function isCategoryFolder(rawName) { return FolderUtils.isCategoryFolder(rawName) }
    function normalizedRemoteFolderName(rawName) { return FolderUtils.normalizedRemoteFolderName(rawName) }
    function isNoSelectFolder(flags) { return FolderUtils.isNoSelectFolder(flags) }
    function isSystemMailboxName(name, specialUse) { return FolderUtils.isSystemMailboxName(name, specialUse) }
    function iconForSpecialUse(specialUse, name) { return FolderUtils.iconForSpecialUse(specialUse, name) }
    function isPrimaryAccountFolderNorm(norm) { return FolderUtils.isPrimaryAccountFolderNorm(norm) }
    function normalizedFolderFromKey(key) { return FolderUtils.normalizedFolderFromKey(key) }

    function tagColorForName(name, usedColors) {
        return ColorUtils.tagColorForName(name, usedColors, Common.KestrelColors.importantYellow)
    }

    function accountFolderItems() {
        var folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        var tabs = root.dataStoreObj ? root.dataStoreObj.inboxCategoryTabs : ["Primary"]
        return FolderUtils.accountFolderItems(folders, tabs, i18n)
    }

    function moreAccountFolderItems() {
        var folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        return FolderUtils.moreAccountFolderItems(folders, root.accountFolderItems(), root.isMoreFolderExpanded, i18n)
    }

    function tagFolderItems() {
        var folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        var tags = (root.dataStoreObj && root.dataStoreObj.tagItems) ? root.dataStoreObj.tagItems() : []
        return FolderUtils.tagFolderItems(folders, tags, root.tagColorForName)
    }

    function folderEntryByKey(key) {
        return FolderUtils.folderEntryByKey(key, root.visibleFavoriteItems(), root.tagFolderItems(),
                                            root.accountFolderItems(), root.moreAccountFolderItems(),
                                            root.allLocalFolderItems())
    }

    function selectedImapFolderName() {
        var folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        return FolderUtils.selectedImapFolderName(root.selectedFolderKey, folders)
    }

    function folderStatsByKey(folderKey, rawFolderName) {
        // Re-establish reactive dependency.
        if (root.dataStoreObj) void root.dataStoreObj.inboxCategoryTabs
        return FolderUtils.folderStatsByKey(root.dataStoreObj, folderKey, rawFolderName)
    }

    function folderTooltipText(displayName, folderKey, rawFolderName) {
        return FolderUtils.folderTooltipText(displayName, folderKey, rawFolderName, root.dataStoreObj)
    }

    function isMoreFolderExpanded(path) {
        var key = (path || "").toString().toLowerCase()
        if (!key.length) return true
        if (moreFolderExpandedState[key] === undefined) return true
        return !!moreFolderExpandedState[key]
    }

    function toggleMoreFolderExpanded(path) {
        var key = (path || "").toString().toLowerCase()
        if (!key.length) return
        var next = Object.assign({}, moreFolderExpandedState)
        next[key] = !isMoreFolderExpanded(key)
        moreFolderExpandedState = next
    }

    // ── Display utility wrappers ──

    function bucketKeyForDate(dateValue) { return DisplayUtils.bucketKeyForDate(dateValue) }
    function senderEmail(senderValue) { return DisplayUtils.senderEmail(senderValue) }
    function senderDomain(senderValue) { return DisplayUtils.senderDomain(senderValue) }
    function avatarInitials(nameValue) { return DisplayUtils.avatarInitials(nameValue) }
    function emphasizeSubjectEmoji(subjectValue) { return DisplayUtils.emphasizeSubjectEmoji(subjectValue) }
    function avatarSourceLabel(urlValue) { return DisplayUtils.avatarSourceLabel(urlValue) }
    function parseMessageKey(key) { return DisplayUtils.parseMessageKey(key) }
    function isBodyHtmlUsable(value) { return DisplayUtils.isBodyHtmlUsable(value) }

    function subjectRichText(subjectValue) {
        return DisplayUtils.subjectRichText(subjectValue, i18n("(No subject)"))
    }

    function formatListDate(dateValue) { return DisplayUtils.formatListDate(dateValue, root._localeFmt) }
    function formatContentDate(dateValue) { return DisplayUtils.formatContentDate(dateValue, root._localeFmt) }

    function _displayNameFromMailbox(rawValue, fallbackText) {
        return DisplayUtils._displayNameFromMailbox(rawValue, fallbackText, DisplayUtils.senderEmail)
    }

    function resolvedMailboxEmail(rawValue, accountEmailHint) {
        return DisplayUtils.resolvedMailboxEmail(rawValue, accountEmailHint)
    }

    function displayNameForAddress(rawAddressValue, accountEmailHint) {
        return DisplayUtils.displayNameForAddress(rawAddressValue, accountEmailHint, root.dataStoreObj)
    }

    function displaySenderName(senderValue, accountEmailHint) {
        return DisplayUtils.displaySenderName(senderValue, accountEmailHint, root.dataStoreObj, i18n("Unknown sender"))
    }

    function displayThreadParticipants(allSendersRaw, accountEmailHint) {
        var myEmails = (root.accountRepositoryObj ? root.accountRepositoryObj.accounts : [])
            .map(function(a) { return (a.email || "").toString().trim().toLowerCase() })
            .filter(function(e) { return e.length > 0 })
        return DisplayUtils.displayThreadParticipants(allSendersRaw, accountEmailHint, myEmails, root.dataStoreObj, i18n("Me"))
    }

    function _titleCaseWords(s) { return DisplayUtils._titleCaseWords(s) }
    function _accountDisplayNameFromEmail(email) { return DisplayUtils._accountDisplayNameFromEmail(email) }

    function displayRecipientName(recipientValue, accountEmailHint) {
        return DisplayUtils.displayRecipientName(recipientValue, accountEmailHint, root.dataStoreObj, i18n("Recipient"))
    }

    function _splitRecipientMailboxes(recipientValue) {
        return DisplayUtils._splitRecipientMailboxes(recipientValue)
    }

    function displayRecipientNames(recipientValue, accountEmailHint) {
        return DisplayUtils.displayRecipientNames(recipientValue, accountEmailHint, root.dataStoreObj, i18n("Recipient"))
    }

    function senderAvatarSources(senderValue, avatarDomainHint, avatarUrlHint, accountEmailHint) {
        var accountEmail = (accountEmailHint || (root.selectedMessageData && root.selectedMessageData.accountEmail)
                              || "").toString().trim().toLowerCase()
        return DisplayUtils.senderAvatarSources(senderValue, accountEmail, root.dataStoreObj)
    }

    // ── Message data helpers ──

    function messageByKey(key) {
        var p = DisplayUtils.parseMessageKey(key)
        if (!p || !root.dataStoreObj || !root.dataStoreObj.messageByKey) return null
        var raw = root.dataStoreObj.messageByKey(p.accountEmail, p.folder, p.uid)
        if (raw && (raw.accountEmail || "") !== "")
            return raw
        return null
    }

    function requestHydrateForMessageKey(key) {
        var p = DisplayUtils.parseMessageKey(key)
        if (!p || !root.imapServiceObj || !root.imapServiceObj.hydrateMessageBody) return
        var row = root.messageByKey(key)
        var bodyLen = row && row.bodyHtml ? row.bodyHtml.toString().length : 0
        var hasUsableHtml = row ? DisplayUtils.isBodyHtmlUsable(row.bodyHtml) : false
        if (!hasUsableHtml) {
            console.log("[hydrate-html-db] ui-request-hydrate", "key=", key, "bodyLen=", bodyLen)
            root.imapServiceObj.hydrateMessageBody(p.accountEmail, p.folder, p.uid)
        } else {
            console.log("[hydrate-html-db] ui-skip-hydrate-usable", "key=", key, "bodyLen=", bodyLen)
        }
    }

    // ── Content pane hover-expand ──

    function canHoverExpandContentPane() {
        if (!root.contentPaneHoverExpandEnabled) return false
        if (!root.selectedMessageData) return false
        return DisplayUtils.isBodyHtmlUsable(root.selectedMessageData.bodyHtml)
    }

    function updateContentPaneHoverExpandState() {
        var shouldExpand = root.contentPaneHoverAreaHovered && root.shiftKeyDown
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

            var restoreWidth = Math.max(320, root.contentPaneHoverPrevMessageListWidth)
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

    // ── Inline status / toast ──

    function showInlineStatus(message, isError, action) {
        var text = (message || "").toString().trim()
        if (!text.length) return

        var now = Date.now()
        var sig = (isError ? "E:" : "I:") + text
        if (sig === root._lastToastMessage && (now - root._lastToastAtMs) < 2500)
            return
        root._lastToastMessage = sig
        root._lastToastAtMs = now

        root.syncStatus = text
        root.syncStatusIsError = !!isError

        var id = ++root.inlineStatusSeq
        var next = root.inlineStatusQueue.slice()
        next.push({ id: id, text: text, isError: !!isError, action: action || null })
        while (next.length > 3) {
            next.shift()
        }
        root.inlineStatusQueue = next
    }

    function dismissInlineStatus(statusId) {
        var next = []
        for (var i = 0; i < root.inlineStatusQueue.length; ++i) {
            var item = root.inlineStatusQueue[i]
            if (item.id !== statusId) next.push(item)
        }
        root.inlineStatusQueue = next
    }

    // ── Compose helpers ──

    function openComposeDialog(contextLabel) {
        composeDialog.openCompose("", "", "")
    }

    function openForwardCompose(subject, body, bodyText, attachments, initialDarkMode) {
        composeDialog.openCompose("", subject || "", body || "", attachments || [], !!initialDarkMode, bodyText || "")
    }

    function _forwardDateText(d) { return ComposeUtils._forwardDateText(d, root._localeFmt) }

    function forwardMessageFromData(d, dateText, attachments, darkMode) {
        if (!d) return
        var quoted = ComposeUtils._buildQuotedContent(d, "Forwarded Message", root._localeFmt)
        root.openForwardCompose(ComposeUtils._fwdSubject(d.subject), quoted.body, quoted.bodyText, attachments || [], darkMode !== undefined ? !!darkMode : root.contentPaneDarkModeEnabled)
    }

    function openComposerTo(address, contextLabel, subjectParam) {
        var email = (address || "").toString().trim()
        var subject = subjectParam || ""
        if (subject === "") {
            subject = (!email.length && root.selectedMessageData && root.selectedMessageData.subject)
                ? i18n("Re: %1").arg(root.selectedMessageData.subject) : ""
        }
        composeDialog.openCompose(email, subject, "")
    }

    function openReplyCompose(d, isReplyAll, darkMode) {
        if (!d) return
        var replyTo = (d.replyTo && d.replyTo.length > 0) ? d.replyTo : d.sender
        var quoted = ComposeUtils._buildQuotedContent(d, "Original Message", root._localeFmt)
        var params = {
            toList: [replyTo],
            subject: ComposeUtils._replySubject(d.subject),
            body: quoted.body,
            bodyText: quoted.bodyText,
            darkMode: !!darkMode
        }
        if (isReplyAll) {
            var myEmails = (root.accountRepositoryObj ? root.accountRepositoryObj.accounts : [])
                             .map(function(a) { return (a.email || "").toLowerCase() })

            function bareEmail(addr) {
                var m = (addr || "").match(/<([^>]+)>/)
                return (m ? m[1] : addr).trim().toLowerCase()
            }

            var toEmails = params.toList.map(bareEmail)

            var candidates = DisplayUtils._splitRecipientMailboxes((d.recipient || "").toString())
                .concat(DisplayUtils._splitRecipientMailboxes((d.cc || "").toString()))

            var seen = {}
            params.ccList = []
            for (var i = 0; i < candidates.length; i++) {
                var addr = candidates[i].trim()
                if (!addr.length) continue
                var em = bareEmail(addr)
                if (!em.length || seen[em]) continue
                seen[em] = true
                if (myEmails.some(function(me) { return me === em })) continue
                if (toEmails.some(function(t) { return t === em })) continue
                params.ccList.push(addr)
            }
        }
        composeDialog.openComposeReply(params)
    }

    // ── Mail actions ──

    function trashFolderForHost(imapHost) { return MailActions.trashFolderForHost(imapHost) }

    function moveMessageToFolder(accountEmail, folder, uid, targetFolder) {
        if (!imapServiceObj) return
        if (root.dataStoreObj)
            root.dataStoreObj.clearNewMessageCounts(root.selectedFolderKey)
        if (root.selectedMessageKey === "msg:" + accountEmail + "|" + folder + "|" + uid)
            root.selectedMessageKey = ""
        var next = Object.assign({}, root.selectedMessageKeys)
        delete next["msg:" + accountEmail + "|" + folder + "|" + uid]
        root.selectedMessageKeys = next
        imapServiceObj.moveMessage(accountEmail, folder, uid, targetFolder)
    }

    function toggleMessageFlagged(accountEmail, folder, uid, currentlyFlagged) {
        if (!imapServiceObj || !accountEmail.length || !uid.length) return
        imapServiceObj.markMessageFlagged(accountEmail, folder, uid, !currentlyFlagged)
    }

    function deleteSelectedMessages() {
        if (!imapServiceObj) return
        var keys = Object.keys(root.selectedMessageKeys)
        if (keys.length > 0) {
            var pendingNow = Object.assign({}, root.pendingDeleteKeys)
            for (var ki = 0; ki < keys.length; ki++) pendingNow[keys[ki]] = true
            root.pendingDeleteKeys = pendingNow

            var currentKey = root.selectedMessageKey
            if (currentKey.length > 0 && keys.indexOf(currentKey) >= 0) {
                var mdl = root.messageListModelObj
                var nextKey = ""
                if (mdl) {
                    var n = mdl.visibleRowCount
                    var currentIdx = -1
                    for (var i = 0; i < n; ++i) {
                        var r = mdl.rowAt(i)
                        if (r && r.messageKey === currentKey) { currentIdx = i; break }
                    }
                    for (var i2 = currentIdx + 1; i2 < n && !nextKey; ++i2) {
                        var r2 = mdl.rowAt(i2)
                        if (r2 && !r2.isHeader && r2.messageKey && keys.indexOf(r2.messageKey.toString()) < 0)
                            nextKey = r2.messageKey.toString()
                    }
                    if (!nextKey) {
                        for (var i3 = currentIdx - 1; i3 >= 0 && !nextKey; --i3) {
                            var r3 = mdl.rowAt(i3)
                            if (r3 && !r3.isHeader && r3.messageKey && keys.indexOf(r3.messageKey.toString()) < 0)
                                nextKey = r3.messageKey.toString()
                        }
                    }
                }
                root.selectedMessageKey = nextKey
            }
            var accs = root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
            for (var ki2 = 0; ki2 < keys.length; ki2++) {
                var p = DisplayUtils.parseMessageKey(keys[ki2])
                if (!p) continue
                var host = ""
                for (var ai = 0; ai < accs.length; ai++) {
                    var acc = typeof accs[ai].toMap === "function" ? accs[ai].toMap() : accs[ai]
                    if ((acc.email || acc["email"] || "") === p.accountEmail) {
                        host = acc.imapHost || acc["imapHost"] || ""
                        break
                    }
                }
                moveMessageToFolder(p.accountEmail, p.folder, p.uid, MailActions.trashFolderForHost(host))
            }
            root.selectedMessageKeys = ({})
        } else if (root.selectedMessageData) {
            var d = root.selectedMessageData
            var accs2 = root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
            var host2 = ""
            for (var ai2 = 0; ai2 < accs2.length; ai2++) {
                var acc2 = typeof accs2[ai2].toMap === "function" ? accs2[ai2].toMap() : accs2[ai2]
                if ((acc2.email || acc2["email"] || "") === d.accountEmail) {
                    host2 = acc2.imapHost || acc2["imapHost"] || ""
                    break
                }
            }
            var deletedKey = root.selectedMessageKey
            var pendingNow2 = Object.assign({}, root.pendingDeleteKeys)
            pendingNow2[deletedKey] = true
            root.pendingDeleteKeys = pendingNow2
            var mdl2 = root.messageListModelObj
            if (mdl2) {
                var n2 = mdl2.visibleRowCount
                var currentIdx2 = -1
                for (var j = 0; j < n2; ++j) {
                    var r4 = mdl2.rowAt(j)
                    if (r4 && r4.messageKey === deletedKey) { currentIdx2 = j; break }
                }
                var nextKey2 = ""
                for (var j2 = currentIdx2 + 1; j2 < n2 && !nextKey2; ++j2) {
                    var r5 = mdl2.rowAt(j2)
                    if (r5 && !r5.isHeader && r5.messageKey) nextKey2 = r5.messageKey.toString()
                }
                if (!nextKey2) {
                    for (var j3 = currentIdx2 - 1; j3 >= 0 && !nextKey2; --j3) {
                        var r6 = mdl2.rowAt(j3)
                        if (r6 && !r6.isHeader && r6.messageKey) nextKey2 = r6.messageKey.toString()
                    }
                }
                root.selectedMessageKey = nextKey2
            }
            moveMessageToFolder(d.accountEmail, d.folder, d.uid, MailActions.trashFolderForHost(host2))
        }
    }

    // ── Bucket expansion ──

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

    onTodayExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("today", root.todayExpanded)
    onYesterdayExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("yesterday", root.yesterdayExpanded)
    onLastWeekExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("lastWeek", root.lastWeekExpanded)
    onTwoWeeksAgoExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("twoWeeksAgo", root.twoWeeksAgoExpanded)
    onOlderExpandedChanged: if (root.messageListModelObj && root.messageListModelObj.setBucketExpanded) root.messageListModelObj.setBucketExpanded("older", root.olderExpanded)

    // ── Sync / bootstrap ──

    function hasFetchedFolders() {
        var folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        return !!folders && folders.length > 0
    }

    function bootstrapSyncIfNeeded() {
        if (!root.imapServiceObj || root.bootstrapFolderSyncRequested) return
        if (root.hasFetchedFolders()) return
        root.bootstrapFolderSyncRequested = true
        root.accountRefreshing = true
        root.imapServiceObj.refreshFolderList()
    }

    function syncSelectedFolder(forceFetch, silent) {
        if (!root.imapServiceObj) return
        if (!root.hasFetchedFolders()) {
            root.bootstrapSyncIfNeeded()
            return
        }
        if (!forceFetch) return

        var folder = root.selectedImapFolderName()
        if (!folder.length) return
        root.accountRefreshing = true
        root.imapServiceObj.syncFolder(folder, !silent)
    }

    function syncMessageListModelSelection() {
        if (!root.messageListModelObj || !root.messageListModelObj.setSelection) return
        var cats = root.categorySelectionExplicit ? (root.selectedFolderCategories || []) : []
        root.messageListModelObj.setSelection(root.selectedFolderKey || "",
                                              cats,
                                              root.selectedCategoryIndex)
    }

    // ── Reauth ──

    function reauthenticateAccount(email) {
        if (!root.accountSetupObj) return
        root.accountSetupObj.email = email
        root.accountSetupObj.discoverProvider()
        root.accountSetupObj.beginOAuth()
        var launchUrl = root.accountSetupObj.oauthUrl ? root.accountSetupObj.oauthUrl.toString() : ""
        if (launchUrl.length > 0) {
            root.showInlineStatus(i18n("Re-authenticating %1 — check your browser.", email), false)
            Qt.openUrlExternally(launchUrl)
        } else {
            root.showInlineStatus(i18n("Failed to start re-authentication for %1.", email), true)
        }
    }

    // ── Calendar helpers ──

    function visibleCalendarSourceIds() { return MailActions.visibleCalendarSourceIds(root.calendarSources) }

    function setCalendarSourceChecked(sourceId, checked) {
        root.calendarSources = MailActions.setCalendarSourceChecked(root.calendarSources, sourceId, checked)
        root.refreshVisibleGoogleWeekEvents()
    }

    function refreshVisibleGoogleWeekEvents() {
        if (!root.imapServiceObj || !root.imapServiceObj.refreshGoogleWeekEvents)
            return
        var ids = root.visibleCalendarSourceIds()
        var b = MailActions.calendarWeekBoundsIso()
        root.imapServiceObj.refreshGoogleWeekEvents(ids, b.startIso, b.endIso)
    }

    function rebuildCalendarSourcesFromGoogle() {
        if (!root.imapServiceObj || !root.imapServiceObj.googleCalendarList)
            return
        var incoming = root.imapServiceObj.googleCalendarList
        var accs = root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
        var primaryEmail = (accs.length > 0 ? String(accs[0].email || "").toLowerCase() : "")
        root.calendarSources = MailActions.rebuildCalendarSourcesFromGoogle(root.calendarSources, incoming, primaryEmail, i18n("Calendar"))
        root.refreshVisibleGoogleWeekEvents()
    }

    // ── Dialogs ──

    AccountWizard.AccountWizardDialog {
        id: accountWizard
        accountSetupObj: root.accountSetupObj
        accountRepositoryObj: root.accountRepositoryObj
        onToastRequested: (message, isError) => root.showInlineStatus(message, isError)
    }

    Compose.Compose {
        id: composeDialog
        accountRepositoryObj: root.accountRepositoryObj
        dataStoreObj: root.dataStoreObj
        imapServiceObj: root.imapServiceObj
        smtpServiceObj: (typeof smtpService !== "undefined") ? smtpService : null
        onSendRequested: root.showInlineStatus(i18n("Message sent"), false)
    }

    // ── Component.onCompleted ──

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
            root._needsAccountWizard = true
        }
        Qt.callLater(function() {
            root.paneAutoToggleEnabled = true
            root.bootstrapSyncIfNeeded()
            if (root.imapServiceObj && root.imapServiceObj.initialize) {
                root.imapServiceObj.initialize()
            }
            if (root.imapServiceObj && root.imapServiceObj.refreshGoogleCalendars) {
                root.imapServiceObj.refreshGoogleCalendars()
            }
        })
    }

    // ── Connections ──

    Connections {
        target: root.imapServiceObj
        function onSyncFinished(ok, message) {
            root.refreshInProgress = false
            root.accountRefreshing = false
            if (ok)
                root.accountConnected = true
            root.showInlineStatus(message, !ok)
            if (ok && root.hasFetchedFolders() && root.bootstrapFolderSyncRequested) {
                root.bootstrapFolderSyncRequested = false
                root.syncSelectedFolder(false)
            } else if (!ok && !root.hasFetchedFolders()) {
                root.bootstrapFolderSyncRequested = false
            }
        }

        function onHydrateStatus(ok, message) {
            root.showInlineStatus(message, !ok)
        }

        function onRealtimeStatus(ok, message) {
            var t = (message || "").toString().toLowerCase()
            if (ok || t.indexOf("reconnected") >= 0 || t.indexOf("restarted") >= 0) {
                root.accountConnected = true
            } else if (t.indexOf("degraded") >= 0 || t.indexOf("disconnected") >= 0 || t.indexOf("failed") >= 0) {
                root.accountConnected = false
            }
            root.showInlineStatus(message, !ok)
        }

        function onAccountThrottled(accountEmail, message) {
            root.accountThrottled = true
            root.accountThrottleMessage = (message || "").toString()
        }

        function onAccountUnthrottled(accountEmail) {
            root.accountThrottled = false
            root.accountThrottleMessage = ""
        }

        function onAccountNeedsReauth(accountEmail) {
            root.accountNeedsReauth = true
            root.accountNeedsReauthEmail = accountEmail
            root.showInlineStatus(
                i18n("Authentication expired for %1.", accountEmail),
                true,
                { label: i18n("Re-authenticate"), callback: function() { root.reauthenticateAccount(accountEmail) } })
        }

        function onSyncActivityChanged(active) {
            root.accountRefreshing = !!active
            root.refreshInProgress = !!active
        }

        function onGoogleCalendarListChanged() {
            root.rebuildCalendarSourcesFromGoogle()
        }

        function onGoogleWeekEventsChanged() {
            root.calendarEvents = root.imapServiceObj ? root.imapServiceObj.googleWeekEvents : []
        }
    }

    Connections {
        target: root.dataStoreObj
        ignoreUnknownSignals: true
        function onFoldersChanged() {
            root.syncMessageListModelSelection()
            if (root.bootstrapFolderSyncRequested && root.hasFetchedFolders()) {
                root.bootstrapFolderSyncRequested = false
                if (root.imapServiceObj) {
                    root.accountRefreshing = true
                    root.refreshInProgress = true
                    root.imapServiceObj.syncAll(false)
                }
            }
        }
        function onInboxChanged() {
        }
        function onNotificationReplyRequested(accountEmail, folder, uid) {
            root.raise()
            root.requestActivate()
            var key = "msg:" + accountEmail + "|" + folder + "|" + uid
            var d = root.dataStoreObj ? root.dataStoreObj.messageByKey(accountEmail, folder, uid) : null
            if (d)
                root.openReplyCompose(d, false, root.contentPaneDarkModeEnabled)
        }
        function onBodyHtmlUpdated(accountEmail, folder, uid) {
            var key = root.selectedMessageKey
            if (!key.length) return
            var p = DisplayUtils.parseMessageKey(key)
            if (!p) return
            var accMatch = p.accountEmail.toLowerCase() === accountEmail.toLowerCase()
            var folderMatch = p.folder.toLowerCase() === folder.toLowerCase()
            var uidMatch = p.uid === uid
            if (accMatch && folderMatch && uidMatch) {
                root._bodyUpdateVersion++
            } else if (accMatch) {
                var d = root.selectedMessageData
                if (d && (d.threadCount || 0) >= 2)
                    root._bodyUpdateVersion++
            }
        }
    }

    Connections {
        target: root.accountRepositoryObj
        ignoreUnknownSignals: true
        function onAccountsChanged() {
            if (root.imapServiceObj && root.imapServiceObj.initialize)
                root.imapServiceObj.initialize()

            root.bootstrapFolderSyncRequested = false
            var hadFolders = root.hasFetchedFolders()
            root.bootstrapSyncIfNeeded()

            if (root.imapServiceObj) {
                root.refreshInProgress = true
                root.accountRefreshing = true
            }

            if (hadFolders) {
                root.syncSelectedFolder(true, true)
            }
            if (root.imapServiceObj && root.imapServiceObj.refreshGoogleCalendars) {
                root.imapServiceObj.refreshGoogleCalendars()
            }
        }
    }

    Connections {
        target: root.accountSetupObj || null
        ignoreUnknownSignals: true
        function onOauthReadyChanged() {
            if (!root.accountNeedsReauth) return
            if (!root.accountSetupObj || !root.accountSetupObj.oauthReady) return
            root.accountNeedsReauth = false
            root.accountNeedsReauthEmail = ""
            root.showInlineStatus(i18n("Re-authentication successful. Reconnecting..."), false)
            if (root.imapServiceObj) {
                root.imapServiceObj.initialize()
                root.imapServiceObj.syncAll(false)
            }
        }
    }

    // ── Property change handlers ──

    onSelectedFolderKeyChanged: {
        if (root.dataStoreObj)
            root.dataStoreObj.clearNewMessageCounts(root.selectedFolderKey)
        root.selectedCategoryIndex = 0
        root.categorySelectionExplicit = (root.selectedFolderCategories && root.selectedFolderCategories.length > 0)
        root.lastClickedMessageIndex = -1
        root.syncMessageListModelSelection()
    }
    onSelectedCategoryIndexChanged: {
        if (root.dataStoreObj && root.selectedFolderCategories && root.selectedFolderCategories.length > 0) {
            var catName = root.selectedFolderCategories[root.selectedCategoryIndex]
            if (catName)
                root.dataStoreObj.clearNewMessageCounts("[gmail]/categories/" + catName.toLowerCase())
        }
        root.syncMessageListModelSelection()
    }
    onSelectedMessageKeyChanged: {
        if (root.dataStoreObj && root.selectedMessageKey.length > 0)
            root.dataStoreObj.clearNewMessageCounts(root.selectedFolderKey)

        markReadTimer.stop()
        if (root.selectedMessageKey.length > 0)
            markReadTimer.restart()

        var row = root.messageByKey(root.selectedMessageKey)
        if (!row) {
            console.log("[hydrate-html-db] ui-selected-no-data", "key=", root.selectedMessageKey)
            root.setContentPaneHoverExpanded(false)
            return
        }

        root.selectedMessageAnchor = {
            accountEmail: row.accountEmail || "",
            sender: row.sender || "",
            subject: row.subject || "",
            receivedAt: row.receivedAt || ""
        }

        var bodyLen = (row.bodyHtml || "").toString().length
        var hasUsableHtml = DisplayUtils.isBodyHtmlUsable(row.bodyHtml)
        console.log("[hydrate-html-db] ui-selected", "key=", root.selectedMessageKey, "bodyLen=", bodyLen, "usable=", hasUsableHtml)
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

    // ── Resize handles (thin edge zones for frameless window) ──

    MouseArea { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.RightEdge) }
    MouseArea { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 3; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.TopEdge) }
    MouseArea { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 3; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.BottomEdge) }

    MouseArea { anchors.left: parent.left; anchors.top: parent.top; width: 3; height: 3; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.TopEdge | Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.top: parent.top; width: 3; height: 3; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.TopEdge | Qt.RightEdge) }
    MouseArea { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 3; height: 3; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 3; height: 3; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }

    // ══════════════════════════════════════════════════════════════
    //  UI layout
    // ══════════════════════════════════════════════════════════════

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppLayout.TitleBar {
            id: titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: root.titleBarHeight
            Layout.topMargin: 0
            appRoot: root
            systemPalette: systemPalette
        }

        AppLayout.MailToolbar {
            Layout.fillWidth: true
            Layout.preferredHeight: root.toolbarHeight
            appRoot: root
        }

        QQC2.SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 0
                implicitHeight: 1
                color: "transparent"
            }

            AppLayout.FolderPane {
                id: leftPaneContainer
                QQC2.SplitView.minimumWidth: root.collapsedRailWidth
                QQC2.SplitView.preferredWidth: root.folderPaneVisible ? root.folderPaneExpandedWidth : root.collapsedRailWidth
                QQC2.SplitView.fillHeight: true
                appRoot: root
                systemPalette: systemPalette
                onFavoritesMenuRequested: (x, y) => favoritesMenu.popup(x, y)
                onLocalFoldersMenuRequested: (x, y) => localFoldersMenu.popup(x, y)
            }

            MessageList.MessageListPane {
                id: messageListPane
                visible: root.activeWorkspace === "mail"
                opacity: (root.activeWorkspace === "mail" && root.contentPaneHoverExpandActive) ? 0 : 1

                Behavior on opacity {
                    NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
                }

                QQC2.SplitView.minimumWidth: visible ? 0 : 0
                QQC2.SplitView.preferredWidth: visible ? root.contentPaneHoverMessageListWidth : 0
                appRoot: root
                systemPalette: systemPalette
            }

            MessageContent.MessageContentPane {
                id: messageContentPane
                visible: root.activeWorkspace === "mail"
                QQC2.SplitView.minimumWidth: visible ? 420 : 0
                QQC2.SplitView.preferredWidth: visible ? 520 : 0
                QQC2.SplitView.fillWidth: root.activeWorkspace === "mail"
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

            Calendar.CalendarPane {
                id: calendarPane
                visible: root.activeWorkspace === "calendar"
                QQC2.SplitView.minimumWidth: visible ? 720 : 0
                QQC2.SplitView.preferredWidth: visible ? 980 : 0
                QQC2.SplitView.fillWidth: root.activeWorkspace === "calendar"
                systemPalette: systemPalette
                allEvents: root.calendarEvents
                visibleCalendarIds: root.visibleCalendarSourceIds()
            }

            AppLayout.RightPane {
                id: rightPaneContainer
                QQC2.SplitView.minimumWidth: root.rightCollapsedRailWidth
                QQC2.SplitView.preferredWidth: root.rightPaneVisible ? root.rightPaneExpandedWidth : root.rightCollapsedRailWidth
                appRoot: root
                systemPalette: systemPalette
            }

            AppLayout.SplitViewOverlay {
                appRoot: root
                messageListPane: messageListPane
                messageContentPane: messageContentPane
                calendarPane: calendarPane
                rightPaneContainer: rightPaneContainer
            }
        }
    }

    AppLayout.WindowResizeHandles {
        grabSize: root.windowEdgeGrabSize
        targetWindow: root
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
        z: 9999
    }

    // ── Context menus ──

    QQC2.Menu {
        id: favoritesMenu
        title: i18n("Favorites")
        Repeater {
            model: root.allFavoriteDefinitions
            delegate: QQC2.MenuItem {
                text: modelData.name
                checkable: true
                checked: {
                    void root._favoritesConfigRev
                    if (!root.dataStoreObj) return index < 3
                    var config = root.dataStoreObj.favoritesConfig()
                    for (var i = 0; i < config.length; ++i)
                        if (config[i].key === modelData.key) return !!config[i].enabled
                    return index < 3
                }
                onTriggered: {
                    if (root.dataStoreObj)
                        root.dataStoreObj.setFavoriteEnabled(modelData.key, checked)
                }
            }
        }
    }

    QQC2.Menu {
        id: localFoldersMenu
        title: i18n("Local Folders")
        QQC2.MenuItem {
            text: i18n("New Folder…")
            icon.name: "folder-new"
            onTriggered: {
                newFolderNameField.text = ""
                newFolderDialog.open()
            }
        }
    }

    QQC2.Dialog {
        id: newFolderDialog
        title: i18n("New Local Folder")
        modal: true
        anchors.centerIn: parent
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel

        Column {
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: i18n("Folder name:") }
            QQC2.TextField {
                id: newFolderNameField
                implicitWidth: 280
                placeholderText: i18n("e.g. Work, Receipts…")
                Keys.onReturnPressed: newFolderDialog.accept()
            }
        }

        onOpened: newFolderNameField.forceActiveFocus()
        onAccepted: {
            var n = newFolderNameField.text.trim()
            if (n.length > 0 && root.dataStoreObj)
                root.dataStoreObj.createUserFolder(n)
            newFolderNameField.text = ""
        }
        onRejected: newFolderNameField.text = ""
    }
}
