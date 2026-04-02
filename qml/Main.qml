import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import Qt5Compat.GraphicalEffects
import QtCore
import org.kde.kirigami as Kirigami

import "components" as Components
import "components/MessageList" as MessageList
import "components/MessageContent" as MessageContent
import "components/Compose" as Compose
import "components/Calendar" as Calendar

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
            if (!activeFocus && !(typeof searchBar !== "undefined" && searchBar.editing))
                forceActiveFocus()
        }
        Component.onCompleted: forceActiveFocus()
    }

    // Emitted when the Wayland compositor re-exposes the window surface
    // after hiding it (minimize, desktop switch, activity switch).
    // WebEngineView instances listen to this to force-reload their content —
    // works around a Chromium/Wayland bug where the GPU surface goes black.
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
            if (typeof searchBar !== "undefined" && searchBar.enterEditing)
                searchBar.enterEditing()
        }
    }

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
    // Set of message keys currently checked in the message list (JS object used as Set).
    property var selectedMessageKeys: ({})
    // Keys of messages currently being moved to trash (for instant visual removal).
    property var pendingDeleteKeys: ({})
    property int lastClickedMessageIndex: -1
    property bool bootstrapFolderSyncRequested: false

    property bool gmailCalendarsExpanded: true

    // Calendar source list is intentionally keyed like Google Calendar list entries
    // (id/summary/backgroundColor). Hook this to real Google list when backend lands.
    property var calendarSources: [
        { account: "gmail", id: "calendar", name: i18n("Calendar"), checked: true, color: "" },
        { account: "gmail", id: "holidays", name: i18n("Holidays in United States"), checked: true, color: "" },
        { account: "gmail", id: "mel", name: i18n("mellanyjo@gmail.com"), checked: false, color: "" },
        { account: "gmail", id: "raven", name: i18n("Raven"), checked: true, color: "" },
        { account: "gmail", id: "sebastian", name: i18n("Sebastian"), checked: true, color: "" },
        { account: "gmail", id: "sandra", name: i18n("sandrmarshall1953@gmail.com"), checked: true, color: "" }
    ]

    property var calendarEvents: []

    function visibleCalendarSourceIds() {
        const out = []
        for (let i = 0; i < root.calendarSources.length; ++i) {
            const c = root.calendarSources[i]
            if (c && c.checked) out.push(String(c.id || ""))
        }
        return out
    }

    function setCalendarSourceChecked(sourceId, checked) {
        const next = []
        for (let i = 0; i < root.calendarSources.length; ++i) {
            const c = root.calendarSources[i]
            if (String(c.id) === String(sourceId))
                next.push({ account: c.account, id: c.id, name: c.name, checked: !!checked, color: c.color })
            else
                next.push(c)
        }
        root.calendarSources = next
        root.refreshVisibleGoogleWeekEvents()
    }

    function calendarWeekBoundsIso() {
        // Monday -> next Monday (matches week header layout Mon..Sun)
        const now = new Date()
        const start = new Date(now)
        start.setHours(0, 0, 0, 0)
        const mondayOffset = (start.getDay() + 6) % 7
        start.setDate(start.getDate() - mondayOffset)
        const end = new Date(start)
        end.setDate(end.getDate() + 7)
        return { startIso: start.toISOString(), endIso: end.toISOString() }
    }

    function refreshVisibleGoogleWeekEvents() {
        if (!root.imapServiceObj || !root.imapServiceObj.refreshGoogleWeekEvents)
            return
        const ids = root.visibleCalendarSourceIds()
        const b = root.calendarWeekBoundsIso()
        root.imapServiceObj.refreshGoogleWeekEvents(ids, b.startIso, b.endIso)
    }

    function rebuildCalendarSourcesFromGoogle() {
        if (!root.imapServiceObj || !root.imapServiceObj.googleCalendarList)
            return

        const incoming = root.imapServiceObj.googleCalendarList
        if (!incoming || incoming.length === 0)
            return

        const checkedMap = ({})
        for (let i = 0; i < root.calendarSources.length; ++i) {
            const c = root.calendarSources[i]
            checkedMap[String(c.id || "")] = !!c.checked
        }

        // Determine the primary account email for sorting.
        const accs = root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
        const primaryEmail = (accs.length > 0 ? String(accs[0].email || "").toLowerCase() : "")

        const next = []
        for (let i = 0; i < incoming.length; ++i) {
            const g = incoming[i]
            const id = String(g.id || "")
            const rawName = String(g.name || g.summary || id)
            const displayName = (id.toLowerCase() === primaryEmail) ? i18n("Calendar") : rawName
            next.push({
                account: "gmail",
                id: id,
                name: displayName,
                checked: checkedMap.hasOwnProperty(id) ? checkedMap[id] : true,
                color: String(g.color || g.backgroundColor || "")
            })
        }

        // Sort: account-matching calendar first, then alphabetical.
        next.sort(function(a, b) {
            const aIsAccount = (a.id.toLowerCase() === primaryEmail) ? 0 : 1
            const bIsAccount = (b.id.toLowerCase() === primaryEmail) ? 0 : 1
            if (aIsAccount !== bIsAccount) return aIsAccount - bIsAccount
            return a.name.localeCompare(b.name)
        })

        root.calendarSources = next
        root.refreshVisibleGoogleWeekEvents()
    }

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

    Connections {
        target: root.dataStoreObj
        function onFavoritesConfigChanged() { root._favoritesConfigRev++ }
        function onUserFoldersChanged()     { root._userFoldersRev++ }
    }

    property int windowEdgeGrabSize: 8
    property int minWindowWidth: 980
    property int minWindowHeight: 640

    // All possible favorites in display order.
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

    // Reactivity version bumped by Connections below.
    property int _favoritesConfigRev: 0
    property int _userFoldersRev: 0

    function visibleFavoriteItems() {
        void root._favoritesConfigRev
        const config = root.dataStoreObj ? root.dataStoreObj.favoritesConfig() : []
        const enabledKeys = new Set()
        for (let i = 0; i < config.length; ++i)
            if (config[i].enabled) enabledKeys.add(config[i].key)
        // Fallback on empty DB (very first run)
        if (enabledKeys.size === 0) {
            enabledKeys.add("all-inboxes")
            enabledKeys.add("unread")
            enabledKeys.add("flagged")
        }
        const out = []
        for (let i = 0; i < root.allFavoriteDefinitions.length; ++i) {
            const f = root.allFavoriteDefinitions[i]
            if (enabledKeys.has(f.key))
                out.push({ key: "favorites:" + f.key, name: f.name, icon: f.icon, categories: [] })
        }
        return out
    }

    // Default local folders always present.
    readonly property var defaultLocalFolderDefs: [
        { key: "local:inbox",  name: i18n("Inbox"),      icon: "mail-folder-inbox"  },
        { key: "local:outbox", name: i18n("Outbox"),     icon: "mail-folder-outbox" },
        { key: "local:sent",   name: i18n("Sent"),       icon: "mail-folder-sent"   },
        { key: "local:trash",  name: i18n("Trash"),      icon: "user-trash"         },
        { key: "local:drafts", name: i18n("Drafts"),     icon: "document-edit"      },
        { key: "local:junk",   name: i18n("Junk Email"), icon: "mail-mark-junk"     }
    ]

    function allLocalFolderItems() {
        void root._userFoldersRev
        const out = root.defaultLocalFolderDefs.map(function(f) {
            return { key: f.key, name: f.name, icon: f.icon, categories: [] }
        })
        const userFolders = root.dataStoreObj ? root.dataStoreObj.userFolders() : []
        for (let i = 0; i < userFolders.length; ++i) {
            const n = (userFolders[i].name || "").toString()
            if (!n.length) continue
            const k = n.toLowerCase().replace(/\s+/g, "-")
            out.push({ key: "local:" + k, name: n, icon: "folder", categories: [] })
        }
        return out
    }

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
        // Match both "\\Noselect" (with backslash from IMAP) and plain "noselect"
        return f.indexOf("\\noselect") >= 0 || f.indexOf("noselect") >= 0
    }

    function isSystemMailboxName(name, specialUse) {
        const n = (name || "").toString().toLowerCase()
        const s = (specialUse || "").toString().toLowerCase()
        if (s.length) return true
        if (n === "inbox" || n === "drafts" || n === "sent" || n === "sent mail" || n === "trash" || n === "junk" || n === "spam" || n === "all mail" || n === "starred" || n === "important") return true
        if (n.indexOf("[gmail]/") === 0) return true
        return false
    }

    function _hash32(s, seed) {
        let h = (seed === undefined ? 2166136261 : seed) >>> 0
        const t = (s || "").toString()
        for (let i = 0; i < t.length; ++i) {
            h ^= (t.charCodeAt(i) & 0xff)
            h = Math.imul(h, 16777619) >>> 0
        }
        return h >>> 0
    }

    function _hsvToHex(hue, sat, val) {
        const s = sat / 255.0
        const v = val / 255.0
        const c = v * s
        const hp = hue / 60.0
        const x = c * (1 - Math.abs((hp % 2) - 1))
        let r1 = 0, g1 = 0, b1 = 0
        if (hp >= 0 && hp < 1) { r1 = c; g1 = x; b1 = 0 }
        else if (hp < 2) { r1 = x; g1 = c; b1 = 0 }
        else if (hp < 3) { r1 = 0; g1 = c; b1 = x }
        else if (hp < 4) { r1 = 0; g1 = x; b1 = c }
        else if (hp < 5) { r1 = x; g1 = 0; b1 = c }
        else { r1 = c; g1 = 0; b1 = x }
        const m = v - c

        function h2(n) {
            const s2 = Math.max(0, Math.min(255, Math.round(n))).toString(16)
            return s2.length === 1 ? "0" + s2 : s2
        }

        return "#" + h2((r1 + m) * 255) + h2((g1 + m) * 255) + h2((b1 + m) * 255)
    }

    function _hexToRgb(hex) {
        const s = (hex || "").toString().trim()
        if (s.length !== 7 || s[0] !== "#") return null
        return {
            r: parseInt(s.slice(1, 3), 16),
            g: parseInt(s.slice(3, 5), 16),
            b: parseInt(s.slice(5, 7), 16)
        }
    }

    function _colorDistSq(a, b) {
        const ca = _hexToRgb(a)
        const cb = _hexToRgb(b)
        if (!ca || !cb) return 0
        const dr = ca.r - cb.r
        const dg = ca.g - cb.g
        const db = ca.b - cb.b
        return dr * dr + dg * dg + db * db
    }

    function _candidateTagColor(name, attempt) {
        const lower = (name || "").toString().toLowerCase()
        const mixed = _hash32(lower + "#" + String(attempt || 0), _hash32(lower, 2166136261))
        let hue = mixed % 360
        if (hue >= 42 && hue <= 62) hue = (hue + 37) % 360 // keep yellow for Important
        const satBuckets = [170, 190, 210, 230, 245]
        const valBuckets = [200, 218, 235, 250]
        const sat = satBuckets[(mixed >>> 8) % satBuckets.length]
        const val = valBuckets[(mixed >>> 11) % valBuckets.length]
        return _hsvToHex(hue, sat, val)
    }

    function tagColorForName(name, usedColors) {
        const lower = (name || "").toString().toLowerCase()
        if (lower === "important") return "#FFD600"

        const used = usedColors || []
        const minDistSq = 9500
        let best = _candidateTagColor(lower, 0)
        let bestScore = -1

        for (let attempt = 0; attempt < 80; ++attempt) {
            const cand = _candidateTagColor(lower, attempt)
            let minSeen = 1e9
            for (let i = 0; i < used.length; ++i)
                minSeen = Math.min(minSeen, _colorDistSq(cand, used[i]))

            if (used.length === 0 || minSeen >= minDistSq)
                return cand

            if (minSeen > bestScore) {
                bestScore = minSeen
                best = cand
            }
        }
        return best
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

        if (!folders || folders.length === 0)
            return []

        // Top-level = folders with a special_use flag.
        // INBOX without a flag is treated as inbox (all IMAP servers have INBOX).
        const bySpecialUse = {}
        const byNorm = {}

        for (let i = 0; i < folders.length; ++i) {
            const f = folders[i]
            const rawName = root.normalizedRemoteFolderName((f.name || "").toString())

            if (!rawName.length) continue
            if (root.isCategoryFolder(rawName)) continue
            if (root.isNoSelectFolder(f.flags)) continue

            let specialUse = (f.specialUse || "").toString().toLowerCase()

            // Fallback: INBOX is always top-level even without a special_use annotation
            if (!specialUse.length && rawName.toLowerCase() === "inbox")
                specialUse = "inbox"

            if (!specialUse.length) continue

            const norm = rawName.toLowerCase()
            if (byNorm[norm]) continue

            // \Spam → display as "Junk Email"
            let displayName = root.displayFolderName(rawName)
            if (specialUse === "junk") displayName = i18n("Junk Email")

            const item = {
                key: "account:" + norm,
                name: displayName,
                rawName: rawName,
                icon: root.iconForSpecialUse(specialUse, rawName),
                categories: [],
                specialUse: specialUse
            }
            byNorm[norm] = item

            if (!bySpecialUse[specialUse])
                bySpecialUse[specialUse] = item
        }

        // Preferred order by special_use, then any remaining alphabetically.
        const preferredUse = ["inbox", "sent", "trash", "drafts", "junk", "all"]
        const ordered = []
        const used = new Set()
        for (let p = 0; p < preferredUse.length; ++p) {
            const item = bySpecialUse[preferredUse[p]]
            if (item && !used.has(item.rawName.toLowerCase())) {
                ordered.push(item)
                used.add(item.rawName.toLowerCase())
            }
        }
        const remaining = Object.keys(byNorm).sort()
        for (let r = 0; r < remaining.length; ++r) {
            if (!used.has(remaining[r])) ordered.push(byNorm[remaining[r]])
        }

        // Attach Gmail category tabs to the inbox entry.
        for (let j = 0; j < ordered.length; ++j) {
            if ((ordered[j].specialUse || "") === "inbox") {
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

        // More contains only hierarchical (slash-delimited) folders.
        // Top-level folders with no special-use are label/tags — they appear in the Tags section.
        const byNorm = {}
        for (let i = 0; i < folders.length; ++i) {
            const f = folders[i]
            const rawName = root.normalizedRemoteFolderName((f.name || "").toString())
            if (!rawName.length) continue
            if (root.isCategoryFolder(rawName)) continue
            if (rawName.indexOf("/") < 0) continue  // top-level → tags, not More

            const norm = rawName.toLowerCase()
            if (primaryKeys[norm]) continue

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
        const byKey = {}

        // Items from the message-label tag table (have counts).
        const tagsFromDb = (root.dataStoreObj && root.dataStoreObj.tagItems) ? root.dataStoreObj.tagItems() : []
        for (let i = 0; i < tagsFromDb.length; ++i) {
            const t = tagsFromDb[i]
            const label = (t.label || "").toString()
            if (!label.length) continue
            const norm = label.toLowerCase()
            byKey[norm] = {
                key: "tag:" + norm,
                name: root.displayFolderName((t.name || label).toString()),
                rawName: label,
                icon: "tag",
                categories: [],
                unread: Number(t.unread || 0),
                total: Number(t.total || 0),
                accentColor: root.tagColorForName(norm)
            }
        }

        // Top-level IMAP folders with no special-use flag are label-folders (tags).
        // E.g. Work, Personal, FooTest, Newsletter — Gmail user labels that also have IMAP folders.
        const folders = root.dataStoreObj ? root.dataStoreObj.folders : []
        for (let i = 0; i < folders.length; ++i) {
            const f = folders[i]
            const rawName = root.normalizedRemoteFolderName((f.name || "").toString())
            if (!rawName.length) continue
            if (root.isCategoryFolder(rawName)) continue
            if (root.isNoSelectFolder(f.flags)) continue
            if ((f.specialUse || "").toString().length) continue  // has special-use → not a tag
            if (rawName.indexOf("/") >= 0) continue               // hierarchical → More, not Tags
            const norm = rawName.toLowerCase()
            if (norm === "inbox") continue                        // INBOX fallback handled separately
            if (byKey[norm]) continue                             // already present from tagItems
            byKey[norm] = {
                key: "tag:" + norm,
                name: root.displayFolderName(rawName),
                rawName: rawName,
                icon: "tag",
                categories: [],
                unread: 0,
                total: 0,
                accentColor: root.tagColorForName(norm)
            }
        }

        const out = Object.values(byKey)
        out.sort(function(a, b) { return a.name.localeCompare(b.name) })

        // Deterministic anti-collision pass for visible tags.
        const usedColors = []
        for (let i = 0; i < out.length; ++i) {
            const item = out[i]
            const norm = ((item.rawName || "").toString() || "").toLowerCase()
            item.accentColor = root.tagColorForName(norm, usedColors)
            usedColors.push(item.accentColor)
        }

        return out
    }

    function folderEntryByKey(key) {
        const groups = [root.visibleFavoriteItems(), root.tagFolderItems(), root.accountFolderItems(), root.moreAccountFolderItems(), root.allLocalFolderItems()]
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
    // Incremented when body HTML is stored for the currently-selected message.
    // selectedMessageData reads this to create a reactive dependency so the
    // content pane updates after async hydration without needing a full inbox reload.
    property int _bodyUpdateVersion: 0
    readonly property var selectedMessageData: {
        void root._bodyUpdateVersion
        return root.messageByKey(root.selectedMessageKey)
    }


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
    readonly property string primaryAccountIcon: "qrc:/qml/gmail_account_icon.svg"

    Components.AccountWizardDialog {
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
                // allow retrying bootstrap discovery on next folder action
                root.bootstrapFolderSyncRequested = false
            }
        }

        function onHydrateStatus(ok, message) {
            root.showInlineStatus(message, !ok)
        }

        function onRealtimeStatus(ok, message) {
            const t = (message || "").toString().toLowerCase()
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
        function onNotificationReplyRequested(accountEmail, folder, uid) {
            // Raise the window and open reply compose for the notified message.
            root.raise()
            root.requestActivate()
            const key = "msg:" + accountEmail + "|" + folder + "|" + uid
            const d = root.dataStoreObj ? root.dataStoreObj.messageByKey(accountEmail, folder, uid) : null
            if (d)
                root.openReplyCompose(d, false, root.contentPaneDarkModeEnabled)
        }
        function onBodyHtmlUpdated(accountEmail, folder, uid) {
            // Bump _bodyUpdateVersion so selectedMessageData re-evaluates and picks up
            // the fresh body from the DB — without a full inbox reload.
            const key = root.selectedMessageKey
            if (!key.length) return
            const p = root.parseMessageKey(key)
            if (!p) return
            const accMatch = p.accountEmail.toLowerCase() === accountEmail.toLowerCase()
            const folderMatch = p.folder.toLowerCase() === folder.toLowerCase()
            const uidMatch = p.uid === uid
            if (accMatch && folderMatch && uidMatch) {
                root._bodyUpdateVersion++
            } else if (accMatch) {
                // Different UID/folder — this may be a thread member being hydrated.
                // If the user is currently viewing a thread, bump so threadMessages re-queries.
                const d = root.selectedMessageData
                if (d && (d.threadCount || 0) >= 2)
                    root._bodyUpdateVersion++
            }
        }
    }

    Connections {
        target: root.accountRepositoryObj
        ignoreUnknownSignals: true
        function onAccountsChanged() {
            // New/updated account (e.g., OAuth just completed + save) should trigger discovery + fetch immediately.
            // Re-initialize to populate the connection pool for the new account.
            if (root.imapServiceObj && root.imapServiceObj.initialize)
                root.imapServiceObj.initialize()

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
            if (root.imapServiceObj && root.imapServiceObj.refreshGoogleCalendars) {
                root.imapServiceObj.refreshGoogleCalendars()
            }
        }
    }

    onSelectedFolderKeyChanged: {
        if (root.dataStoreObj)
            root.dataStoreObj.clearNewMessageCounts(root.selectedFolderKey)
        root.selectedCategoryIndex = 0
        root.categorySelectionExplicit = (root.selectedFolderCategories && root.selectedFolderCategories.length > 0)
        root.lastClickedMessageIndex = -1
        root.syncMessageListModelSelection()
    }
    onSelectedCategoryIndexChanged: {
        // Clear the category's new-message count when its tab is clicked.
        if (root.dataStoreObj && root.selectedFolderCategories && root.selectedFolderCategories.length > 0) {
            const catName = root.selectedFolderCategories[root.selectedCategoryIndex]
            if (catName)
                root.dataStoreObj.clearNewMessageCounts("[gmail]/categories/" + catName.toLowerCase())
        }
        root.syncMessageListModelSelection()
    }
    onSelectedMessageKeyChanged: {
        // Any message interaction clears the new-message badge for the current folder.
        if (root.dataStoreObj && root.selectedMessageKey.length > 0)
            root.dataStoreObj.clearNewMessageCounts(root.selectedFolderKey)

        markReadTimer.stop()
        if (root.selectedMessageKey.length > 0)
            markReadTimer.restart()

        const row = root.messageByKey(root.selectedMessageKey)
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

        const bodyLen = (row.bodyHtml || "").toString().length
        const hasUsableHtml = root.isBodyHtmlUsable(row.bodyHtml)
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

    // Returns a comma-separated string of unique participant display names for a
    // thread, with the current user's accounts replaced by "Me" and "Me" sorted last.
    function displayThreadParticipants(allSendersRaw, accountEmailHint) {
        const raw = (allSendersRaw || "").toString()
        if (!raw.length) return ""

        // Collect all account emails for "Me" detection
        const myEmails = (root.accountRepositoryObj ? root.accountRepositoryObj.accounts : [])
            .map(function(a) { return (a.email || "").toString().trim().toLowerCase() })
            .filter(function(e) { return e.length > 0 })

        const parts = raw.split("\x1f")
        const seen = {}
        const others = []
        let hasSelf = false

        for (let i = 0; i < parts.length; ++i) {
            const sender = (parts[i] || "").trim()
            if (!sender.length) continue

            // Extract email from "Display Name <email>" or plain email
            const m = sender.match(/<([^>]+)>/)
            const email = (m ? m[1] : sender).trim().toLowerCase()

            const isSelf = myEmails.some(function(me) { return me === email })
            if (isSelf) {
                hasSelf = true
                continue
            }

            // Get display name
            const label = root.displaySenderName(sender, accountEmailHint)
            const key = label.toLowerCase()
            if (!seen[key]) {
                seen[key] = true
                others.push(label)
            }
        }

        if (hasSelf) others.push(i18n("Me"))
        return others.join(", ")
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
        if (!email.length || (accountEmail.length && email === accountEmail))
            return []
        if (!root.dataStoreObj || !root.dataStoreObj.avatarForEmail)
            return []
        const url = (root.dataStoreObj.avatarForEmail(email) || "").toString().trim()
        return url.length ? [url] : []
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
        if (/\bsrc\s*=\s*["']\s*cid:/i.test(html)) return false
        const hasHtmlish = /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html)
        return hasHtmlish
    }

    function messageByKey(key) {
        const p = root.parseMessageKey(key)
        if (!p || !root.dataStoreObj || !root.dataStoreObj.messageByKey) return null

        const raw = root.dataStoreObj.messageByKey(p.accountEmail, p.folder, p.uid)
        // C++ returns an empty {} QVariantMap when no row is found.
        if (raw && (raw.accountEmail || "") !== "")
            return raw
        return null
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
        const bodyLen = row && row.bodyHtml ? row.bodyHtml.toString().length : 0
        const hasUsableHtml = row ? root.isBodyHtmlUsable(row.bodyHtml) : false
        if (!hasUsableHtml) {
            console.log("[hydrate-html-db] ui-request-hydrate", "key=", key, "bodyLen=", bodyLen)
            root.imapServiceObj.hydrateMessageBody(p.accountEmail, p.folder, p.uid)
        } else {
            console.log("[hydrate-html-db] ui-skip-hydrate-usable", "key=", key, "bodyLen=", bodyLen)
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

    function showInlineStatus(message, isError, action) {
        const text = (message || "").toString().trim()
        if (!text.length) return

        const now = Date.now()
        const sig = (isError ? "E:" : "I:") + text
        if (sig === root._lastToastMessage && (now - root._lastToastAtMs) < 2500)
            return
        root._lastToastMessage = sig
        root._lastToastAtMs = now

        root.syncStatus = text
        root.syncStatusIsError = !!isError

        const id = ++root.inlineStatusSeq
        const next = root.inlineStatusQueue.slice()
        next.push({ id: id, text: text, isError: !!isError, action: action || null })
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
        composeDialog.openCompose("", "", "")
    }

    function openForwardCompose(subject, body, bodyText, attachments, initialDarkMode) {
        composeDialog.openCompose("", subject || "", body || "", attachments || [], !!initialDarkMode, bodyText || "")
    }

    function _forwardDateText(d) {
        if (!d || !d.receivedAt)
            return ""
        return new Date(d.receivedAt).toLocaleString(Qt.locale(), "ddd M/d/yyyy h:mm AP")
    }

    // Builds {body, bodyText} for a quoted message compose window.
    // body    → original HTML for WebEngineView (empty if plain-text source)
    // bodyText → RichText header + optional plain text for the editable TextArea
    function _buildQuotedContent(d, headerLabel) {
        function esc(s) {
            return (s || "").toString()
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
        }
        function mailtoLink(email) {
            return '<a href="mailto:' + email + '">' + esc(email) + '</a>'
        }
        function _fmtFromHtml(raw) {
            const s = (raw || "").toString().trim()
            const lt = s.lastIndexOf('<')
            const gt = s.lastIndexOf('>')
            if (lt > 0 && gt > lt) {
                const name = s.slice(0, lt).trim()
                const email = s.slice(lt + 1, gt).trim()
                return name.length
                    ? '&quot;' + esc(name) + '&quot; &lt;' + mailtoLink(email) + '&gt;'
                    : mailtoLink(email)
            }
            return s.includes('@') ? mailtoLink(s) : esc(s)
        }
        function _fmtToHtml(raw) {
            const s = (raw || "").toString().trim()
            const lt = s.lastIndexOf('<')
            const gt = s.lastIndexOf('>')
            const email = (lt >= 0 && gt > lt) ? s.slice(lt + 1, gt).trim() : s
            return email.includes('@') ? mailtoLink(email) : esc(email)
        }

        const originalHtml = (d.bodyHtml && d.bodyHtml.toString().length > 0) ? d.bodyHtml.toString() : ""
        const originalText = (d.body && d.body.toString().length > 0) ? d.body.toString() : ((d.snippet || "").toString())
        const senderRaw = (d.sender && d.sender.toString().length > 0) ? d.sender.toString() : (d.accountEmail || "").toString()
        const toRaw = (d.recipient || "").toString()
        const msgDate = d.receivedAt
            ? new Date(d.receivedAt).toLocaleString(Qt.locale(), "M/d/yyyy h:mm:ss AP")
            : ""

        const header = "<br>------ " + headerLabel + " ------<br>"
                     + "From: " + _fmtFromHtml(senderRaw) + "<br>"
                     + "To: " + _fmtToHtml(toRaw) + "<br>"
                     + (msgDate ? "Date: " + esc(msgDate) + "<br>" : "")
                     + "Subject: " + esc((d.subject || "").toString()) + "<br><br>"

        if (originalHtml.length > 0)
            return { body: originalHtml, bodyText: header }

        const escapedText = esc(originalText).replace(/\n/g, "<br>")
        return { body: "", bodyText: header + escapedText }
    }

    function forwardMessageFromData(d, dateText, attachments, darkMode) {
        if (!d) return
        const quoted = root._buildQuotedContent(d, "Forwarded Message")
        root.openForwardCompose(root._fwdSubject(d.subject), quoted.body, quoted.bodyText, attachments || [], darkMode !== undefined ? !!darkMode : root.contentPaneDarkModeEnabled)
    }

    function openComposerTo(address, contextLabel, subjectParam = "") {
        const email = (address || "").toString().trim()
        let subject = subjectParam
        if (subject === "") {
            subject = (!email.length && root.selectedMessageData && root.selectedMessageData.subject)
                ? i18n("Re: %1").arg(root.selectedMessageData.subject) : ""
        }
        composeDialog.openCompose(email, subject, "")
    }

    function openReplyCompose(d, isReplyAll, darkMode) {
        if (!d) return
        const replyTo = (d.replyTo && d.replyTo.length > 0) ? d.replyTo : d.sender
        const quoted = root._buildQuotedContent(d, "Original Message")
        const params = {
            toList: [replyTo],
            subject: root._replySubject(d.subject),
            body: quoted.body,
            bodyText: quoted.bodyText,
            darkMode: !!darkMode
        }
        if (isReplyAll) {
            const myEmails = (root.accountRepositoryObj ? root.accountRepositoryObj.accounts : [])
                             .map(a => (a.email || "").toLowerCase())

            // Extract bare email from "Display Name <email>" or plain email
            function bareEmail(addr) {
                const m = (addr || "").match(/<([^>]+)>/)
                return (m ? m[1] : addr).trim().toLowerCase()
            }

            // Addresses already going into TO — don't duplicate in CC
            const toEmails = params.toList.map(bareEmail)

            // Original TO + original CC are all CC candidates for reply-all
            const candidates = root._splitRecipientMailboxes((d.recipient || "").toString())
                .concat(root._splitRecipientMailboxes((d.cc || "").toString()))

            const seen = {}
            params.ccList = []
            for (let i = 0; i < candidates.length; i++) {
                const addr = candidates[i].trim()
                if (!addr.length) continue
                const email = bareEmail(addr)
                if (!email.length || seen[email]) continue
                seen[email] = true
                if (myEmails.some(me => me === email)) continue   // skip self
                if (toEmails.some(t => t === email)) continue     // skip already-in-TO
                params.ccList.push(addr)
            }
        }
        composeDialog.openComposeReply(params)
    }

    function _replySubject(subj) {
        const s = (subj || "").toString().trim()
        return s.toLowerCase().startsWith("re:") ? s : "Re: " + s
    }

    function _fwdSubject(subj) {
        const s = (subj || "").toString().trim()
        const l = s.toLowerCase()
        return (l.startsWith("fwd:") || l.startsWith("fw:")) ? s : "Fwd: " + s
    }

    function _quotedBody(d) {
        const sender = (d.sender || "").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
        const subj   = (d.subject || "").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
        const date   = d.receivedAt ? new Date(d.receivedAt).toLocaleString(Qt.locale(), "ddd M/d/yyyy h:mm AP") : ""
        const body   = (d.bodyHtml && d.bodyHtml.toString().length > 0) ? d.bodyHtml : (d.snippet || "")
        return '<br><br><blockquote style="margin:0 0 0 6px;padding-left:8px;border-left:2px solid #999;color:inherit;">'
             + '<b>From:</b> ' + sender + '<br>'
             + (date ? '<b>Date:</b> ' + date + '<br>' : '')
             + '<b>Subject:</b> ' + subj + '<br><br>'
             + body
             + '</blockquote>'
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
        // Any mutation clears the new-message badge for the current folder.
        if (root.dataStoreObj)
            root.dataStoreObj.clearNewMessageCounts(root.selectedFolderKey)
        // Deselect if this was the viewed message
        if (root.selectedMessageKey === "msg:" + accountEmail + "|" + folder + "|" + uid)
            root.selectedMessageKey = ""
        // Uncheck from multi-select set
        const next = Object.assign({}, root.selectedMessageKeys)
        delete next["msg:" + accountEmail + "|" + folder + "|" + uid]
        root.selectedMessageKeys = next
        imapServiceObj.moveMessage(accountEmail, folder, uid, targetFolder)
    }

    function toggleMessageFlagged(accountEmail, folder, uid, currentlyFlagged) {
        if (!imapServiceObj || !accountEmail.length || !uid.length) return
        imapServiceObj.markMessageFlagged(accountEmail, folder, uid, !currentlyFlagged)
    }

    // Delete all checked messages (or the currently viewed one if nothing is checked).
    // "Delete" means move to Trash.
    function deleteSelectedMessages() {
        if (!imapServiceObj) return
        const keys = Object.keys(root.selectedMessageKeys)
        if (keys.length > 0) {
            // Mark rows for instant visual collapse.
            const pendingNow = Object.assign({}, root.pendingDeleteKeys)
            for (const k of keys) pendingNow[k] = true
            root.pendingDeleteKeys = pendingNow

            // If the currently viewed message is being deleted, auto-select the next one.
            // (The hover-trash icon uses selectedMessageKeys even for a single message.)
            const currentKey = root.selectedMessageKey
            if (currentKey.length > 0 && keys.indexOf(currentKey) >= 0) {
                const mdl = root.messageListModelObj
                let nextKey = ""
                if (mdl) {
                    const n = mdl.visibleRowCount
                    let currentIdx = -1
                    for (let i = 0; i < n; ++i) {
                        const r = mdl.rowAt(i)
                        if (r && r.messageKey === currentKey) { currentIdx = i; break }
                    }
                    for (let i = currentIdx + 1; i < n && !nextKey; ++i) {
                        const r = mdl.rowAt(i)
                        if (r && !r.isHeader && r.messageKey && keys.indexOf(r.messageKey.toString()) < 0)
                            nextKey = r.messageKey.toString()
                    }
                    if (!nextKey) {
                        for (let i = currentIdx - 1; i >= 0 && !nextKey; --i) {
                            const r = mdl.rowAt(i)
                            if (r && !r.isHeader && r.messageKey && keys.indexOf(r.messageKey.toString()) < 0)
                                nextKey = r.messageKey.toString()
                        }
                    }
                }
                root.selectedMessageKey = nextKey
            }
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
        } else if (root.selectedMessageData) {
            // Single message — current viewed message (e.g. toolbar Delete button)
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
            // Mark row for instant visual collapse.
            const deletedKey = root.selectedMessageKey
            const pendingNow2 = Object.assign({}, root.pendingDeleteKeys)
            pendingNow2[deletedKey] = true
            root.pendingDeleteKeys = pendingNow2
            // Auto-select the next message before removing the current one.
            const mdl = root.messageListModelObj
            if (mdl) {
                const n = mdl.visibleRowCount
                let currentIdx = -1
                for (let i = 0; i < n; ++i) {
                    const r = mdl.rowAt(i)
                    if (r && r.messageKey === deletedKey) { currentIdx = i; break }
                }
                let nextKey = ""
                for (let i = currentIdx + 1; i < n && !nextKey; ++i) {
                    const r = mdl.rowAt(i)
                    if (r && !r.isHeader && r.messageKey) nextKey = r.messageKey.toString()
                }
                if (!nextKey) {
                    for (let i = currentIdx - 1; i >= 0 && !nextKey; --i) {
                        const r = mdl.rowAt(i)
                        if (r && !r.isHeader && r.messageKey) nextKey = r.messageKey.toString()
                    }
                }
                root.selectedMessageKey = nextKey
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

    function reauthenticateAccount(email) {
        if (!root.accountSetupObj) return
        root.accountSetupObj.email = email
        root.accountSetupObj.discoverProvider()
        root.accountSetupObj.beginOAuth()
        const launchUrl = root.accountSetupObj.oauthUrl ? root.accountSetupObj.oauthUrl.toString() : ""
        if (launchUrl.length > 0) {
            root.showInlineStatus(i18n("Re-authenticating %1 — check your browser.", email), false)
            Qt.openUrlExternally(launchUrl)
        } else {
            root.showInlineStatus(i18n("Failed to start re-authentication for %1.", email), true)
        }
    }

    // When OAuth completes after a re-auth, clear the flag and reinitialize IMAP.
    Connections {
        target: root.accountSetupObj || null
        ignoreUnknownSignals: true
        function onOauthReadyChanged() {
            if (!root.accountNeedsReauth) return
            if (!root.accountSetupObj || !root.accountSetupObj.oauthReady) return
            root.accountNeedsReauth = false
            root.accountNeedsReauthEmail = ""
            root.showInlineStatus(i18n("Re-authentication successful. Reconnecting..."), false)
            // The new refresh token is already in the vault. Don't call
            // saveCurrentAccount — it would overwrite account fields with
            // nulls if provider discovery hasn't completed. Re-initialize
            // to populate the connection pool (idempotent for workers), then sync.
            if (root.imapServiceObj) {
                root.imapServiceObj.initialize()
                root.imapServiceObj.syncAll(false)
            }
        }
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
        // Re-establish reactive dependency for count tooltips/badges when datastore emits dataChanged.
        // inboxCategoryTabs is lightweight and NOTIFY-wired to dataChanged.
        const _statsDep = root.dataStoreObj.inboxCategoryTabs
        return root.dataStoreObj.statsForFolder(folderKey || "", rawFolderName || "")
    }

    function folderTooltipText(displayName, folderKey, rawFolderName) {
        const s = root.folderStatsByKey(folderKey, rawFolderName)
        return displayName + " - " + s.total + " items (" + s.unread + " unread)"
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

                Components.SearchBar {
                    id: searchBar
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 5
                    appRoot: root
                    inactiveWidth: root.titleSearchWidth
                    activeWidth: root.titleSearchWidth + Kirigami.Units.gridUnit * 8

                    onSearchRequested: function(query) {
                        if (root.messageListModelObj)
                            root.messageListModelObj.setSearchQuery(query)
                    }
                    onSearchCleared: {
                        if (root.messageListModelObj)
                            root.messageListModelObj.setSearchQuery("")
                        root.syncMessageListModelSelection()
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
                        onTriggered: (actionText) => {
                            if (actionText === "" || actionText === i18n("Mail..."))
                                root.openComposeDialog()
                        }
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

                    Components.MailActionButton {
                        iconName: "mail-reply-sender"
                        text: i18n("Reply")
                        menuItems: [
                            { text: i18n("Reply"),        icon: "mail-reply-sender" },
                            { text: i18n("Reply to all"), icon: "mail-reply-all"    }
                        ]
                        onTriggered: (actionText) => {
                            const d = root.selectedMessageData; if (!d) return
                            root.openReplyCompose(d, actionText === i18n("Reply to all"), root.contentPaneDarkModeEnabled)
                        }
                    }
                    Components.MailActionButton {
                        iconName: "mail-reply-all"
                        text: i18n("Reply All")
                        onTriggered: {
                            const d = root.selectedMessageData; if (!d) return
                            root.openReplyCompose(d, true, root.contentPaneDarkModeEnabled)
                        }
                    }
                    Components.MailActionButton {
                        iconName: "mail-forward"
                        text: i18n("Forward")
                        onTriggered: {
                            const d = root.selectedMessageData; if (!d) return
                            // Kick off downloads now so they're ready (or in flight) by the time Send is clicked.
                            if (messageContentPane && messageContentPane.startAllAttachmentPrefetchForCurrentMessage)
                                messageContentPane.startAllAttachmentPrefetchForCurrentMessage()
                            const a = (messageContentPane && messageContentPane.forwardAttachmentPathsForCurrentMessage)
                                        ? messageContentPane.forwardAttachmentPathsForCurrentMessage() : []
                            root.forwardMessageFromData(d, root._forwardDateText(d), a)
                        }
                    }
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
                        title: root.activeWorkspace === "calendar" ? i18n("Calendar") : i18n("Mail")
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
                        id: favoritesSectionBtn
                        visible: root.activeWorkspace === "mail"
                        expanded: root.favoritesExpanded
                        sectionIcon: "favorite"
                        title: i18n("Favorites")
                        titleOpacity: 0.8
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        onActivated: root.favoritesExpanded = !root.favoritesExpanded
                        onContextMenuRequested: (x, y) => favoritesMenu.popup(x, y)
                    }

                    Repeater {
                        model: (root.activeWorkspace === "mail" && root.favoritesExpanded) ? root.visibleFavoriteItems() : []
                        delegate: Components.FolderItemDelegate {
                            readonly property var folderStats: root.folderStatsByKey(modelData.key, "")
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            unreadCount: folderStats.unread
                            newMessageCount: folderStats.newMessages || 0
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, "")
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Components.FolderSectionButton {
                        visible: root.activeWorkspace === "mail"
                        Layout.topMargin: 6
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
                        model: (root.activeWorkspace === "mail" && root.tagsExpanded) ? root.tagFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            readonly property string rawFolderName: (modelData.rawName || modelData.name || "")
                            readonly property var folderStats: root.folderStatsByKey(modelData.key, rawFolderName)
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            iconColor: (modelData.accentColor || "transparent")
                            unreadCount: Number(modelData.unread || 0)
                            newMessageCount: folderStats.newMessages || 0
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Components.FolderSectionButton {
                        visible: root.activeWorkspace === "mail"
                        Layout.topMargin: 6
                        expanded: root.accountExpanded
                        sectionIcon: root.primaryAccountIcon
                        title: root.primaryAccountName
                        titleOpacity: 0.85
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        rightActivityIcon: root.accountRefreshing ? "view-refresh" : ""
                        rightActivitySpinning: root.accountRefreshing
                        rightStatusIcon: root.accountNeedsReauth ? "dialog-password" : (root.accountConnected ? "network-connect" : "network-disconnect")
                        rightThrottleIcon: root.accountThrottled ? "dialog-warning" : ""
                        rightThrottleTooltip: root.accountNeedsReauth
                            ? i18n("Authentication expired. Click to re-authenticate.")
                            : root.accountThrottled
                              ? i18n("Account is being throttled. Sync and body hydration are slowed by provider/pool limits. Wait a few minutes, reduce concurrent refreshes, or pause heavy background tasks.")
                              : ""
                        onActivated: {
                            if (root.accountNeedsReauth) {
                                root.reauthenticateAccount(root.accountNeedsReauthEmail)
                            } else {
                                root.accountExpanded = !root.accountExpanded
                            }
                        }
                    }

                    Repeater {
                        model: (root.activeWorkspace === "mail" && root.accountExpanded) ? root.accountFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            readonly property string rawFolderName: (modelData.rawName || modelData.name || "")
                            readonly property var folderStats: root.folderStatsByKey(modelData.key, rawFolderName)
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            unreadCount: folderStats.unread
                            newMessageCount: folderStats.newMessages || 0
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    // "More" is a collapsible row nested under the account (indentLevel 1).
                    Components.FolderItemDelegate {
                        visible: root.activeWorkspace === "mail" && root.accountExpanded
                        rowHeight: root.folderRowHeight
                        iconSize: root.folderListIconSize
                        folderKey: ""
                        folderName: i18n("More")
                        folderIcon: "overflow-menu-horizontal"
                        indentLevel: 1
                        hasChildren: true
                        expanded: root.moreExpanded
                        onToggleRequested: root.moreExpanded = !root.moreExpanded
                    }

                    Repeater {
                        model: (root.activeWorkspace === "mail" && root.accountExpanded && root.moreExpanded) ? root.moreAccountFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            readonly property string rawFolderName: (modelData.rawName || modelData.name || "")
                            readonly property var folderStats: root.folderStatsByKey(modelData.key, rawFolderName)
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            indentLevel: 2 + Number(modelData.level || 0)
                            hasChildren: !!modelData.hasChildren
                            expanded: !!modelData.expanded
                            unreadCount: modelData.noselect ? 0 : folderStats.unread
                            newMessageCount: modelData.noselect ? 0 : (folderStats.newMessages || 0)
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, rawFolderName)
                            onToggleRequested: root.toggleMoreFolderExpanded(rawFolderName)
                            onActivated: {
                                if (!modelData.noselect) root.selectedFolderKey = modelData.key
                            }
                        }
                    }

                    Components.FolderSectionButton {
                        id: localFoldersSectionBtn
                        visible: root.activeWorkspace === "mail"
                        Layout.topMargin: 6
                        expanded: root.localFoldersExpanded
                        sectionIcon: "folder"
                        title: i18n("Local Folders")
                        titleOpacity: 0.85
                        rowHeight: root.folderRowHeight
                        chevronSize: root.sectionChevronSize
                        sectionIconSize: root.folderListSectionIconSize
                        onActivated: root.localFoldersExpanded = !root.localFoldersExpanded
                        onContextMenuRequested: (x, y) => localFoldersMenu.popup(x, y)
                    }

                    Repeater {
                        model: (root.activeWorkspace === "mail" && root.localFoldersExpanded) ? root.allLocalFolderItems() : []
                        delegate: Components.FolderItemDelegate {
                            readonly property var folderStats: root.folderStatsByKey(modelData.key, "")
                            rowHeight: root.folderRowHeight
                            iconSize: root.folderListIconSize
                            indentLevel: 1
                            folderKey: modelData.key
                            folderName: modelData.name
                            folderIcon: modelData.icon
                            unreadCount: folderStats.unread
                            newMessageCount: folderStats.newMessages || 0
                            selected: root.selectedFolderKey === modelData.key
                            tooltipText: root.folderTooltipText(modelData.name, modelData.key, "")
                            onActivated: root.selectedFolderKey = modelData.key
                        }
                    }

                    Calendar.CalendarSidebarPane {
                        Layout.fillWidth: true
                        visibleInCalendar: root.activeWorkspace === "calendar"
                        gmailCalendarsExpanded: root.gmailCalendarsExpanded
                        calendarSources: root.calendarSources
                        accountIcon: root.primaryAccountIcon
                        onExpandedToggled: (expanded) => root.gmailCalendarsExpanded = expanded
                        onSourceToggled: (sourceId, checked) => root.setCalendarSourceChecked(sourceId, checked)
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
                            { iconName: "mail-message", label: i18n("Mail"), toolTipText: i18n("Mail"), active: root.activeWorkspace === "mail" },
                            { iconName: "office-calendar", label: i18n("Calendar"), toolTipText: i18n("Calendar"), active: root.activeWorkspace === "calendar" },
                            { iconName: "user-identity", label: i18n("People"), toolTipText: i18n("People") },
                            { iconName: "overflow-menu-horizontal", label: i18n("More"), toolTipText: i18n("More"), useHorizontalDots: true }
                        ]
                        onItemClicked: function(_index, item) {
                            const icon = (item && item.iconName) ? item.iconName : "";
                            if (icon === "mail-message")
                                root.activeWorkspace = "mail";
                            else if (icon === "office-calendar")
                                root.activeWorkspace = "calendar";
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
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: ""; showLabel: false; toolTipText: i18n("Mail"); active: root.activeWorkspace === "mail"; hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4; onClicked: root.activeWorkspace = "mail" }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: ""; showLabel: false; toolTipText: i18n("Calendar"); active: root.activeWorkspace === "calendar"; hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4; onClicked: root.activeWorkspace = "calendar" }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: ""; showLabel: false; toolTipText: i18n("People"); hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "view-task"; label: ""; showLabel: false; toolTipText: i18n("Tasks"); hoverFeedback: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "overflow-menu-horizontal"; label: ""; showLabel: false; hoverFeedback: true; toolTipText: i18n("More"); useHorizontalDots: true; underlineOnLeft: true; sideIndicatorInset: 4 }
                    }
                }
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
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: i18n("Mail"); toolTipText: i18n("Mail"); showLabel: false; active: root.activeWorkspace === "mail"; hoverFeedback: true; onClicked: root.activeWorkspace = "mail" }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: i18n("Calendar"); toolTipText: i18n("Calendar"); showLabel: false; active: root.activeWorkspace === "calendar"; hoverFeedback: true; onClicked: root.activeWorkspace = "calendar" }
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
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: ""; showLabel: false; toolTipText: i18n("Mail"); active: root.activeWorkspace === "mail"; hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4; onClicked: root.activeWorkspace = "mail" }
                        Components.PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: ""; showLabel: false; toolTipText: i18n("Calendar"); active: root.activeWorkspace === "calendar"; hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4; onClicked: root.activeWorkspace = "calendar" }
                    }
                }
            }

            Item {
                anchors.fill: parent
                z: 50

                // Visual splitter lines (non-interactive).
                Rectangle {
                    visible: messageListPane.visible
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    x: Math.round(messageListPane.x)
                }
                Rectangle {
                    visible: root.activeWorkspace === "mail" ? messageContentPane.visible : calendarPane.visible
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    x: Math.round(root.activeWorkspace === "mail" ? messageContentPane.x : calendarPane.x)
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

    // --- Favorites context menu (right-click on Favorites section header) ---
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
                    const config = root.dataStoreObj.favoritesConfig()
                    for (let i = 0; i < config.length; ++i)
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

    // --- Local Folders context menu (right-click on Local Folders section header) ---
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

    // --- New Folder dialog ---
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
            const n = newFolderNameField.text.trim()
            if (n.length > 0 && root.dataStoreObj)
                root.dataStoreObj.createUserFolder(n)
            newFolderNameField.text = ""
        }
        onRejected: newFolderNameField.text = ""
    }

}
