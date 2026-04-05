.pragma library

function displayFolderName(rawName) {
    var name = (rawName || "").toString()
    var clean = name
    var idxBracket = name.lastIndexOf("]/")
    if (idxBracket >= 0 && idxBracket + 2 < name.length) clean = name.slice(idxBracket + 2)
    var idxSlash = clean.lastIndexOf("/")
    if (idxSlash >= 0 && idxSlash + 1 < clean.length) clean = clean.slice(idxSlash + 1)
    if (!clean.length) return name

    var lower = clean.toLowerCase()
    return lower.replace(/\b\w/g, function(c) { return c.toUpperCase() })
}

function isCategoryFolder(rawName) {
    var n = (rawName || "").toString().toLowerCase()
    return n.indexOf("/categories/") >= 0
}

function normalizedRemoteFolderName(rawName) {
    var n = (rawName || "").toString()
    if (n.toLowerCase().indexOf("[google mail]/") === 0) {
        return "[Gmail]/" + n.slice("[Google Mail]/".length)
    }
    return n
}

function isNoSelectFolder(flags) {
    var f = (flags || "").toString().toLowerCase()
    return f.indexOf("\\noselect") >= 0 || f.indexOf("noselect") >= 0
}

function isSystemMailboxName(name, specialUse) {
    var n = (name || "").toString().toLowerCase()
    var s = (specialUse || "").toString().toLowerCase()
    if (s.length) return true
    if (n === "inbox" || n === "drafts" || n === "sent" || n === "sent mail" || n === "trash" || n === "junk" || n === "spam" || n === "all mail" || n === "starred" || n === "important") return true
    if (n.indexOf("[gmail]/") === 0) return true
    return false
}

function iconForSpecialUse(specialUse, name) {
    var s = (specialUse || "").toLowerCase()
    var n = (name || "").toLowerCase()
    if (s === "inbox" || n === "inbox") return "mail-folder-inbox"
    if (s === "sent" || n.indexOf("sent") >= 0) return "mail-folder-sent"
    if (s === "trash" || n.indexOf("trash") >= 0 || n.indexOf("bin") >= 0) return "user-trash"
    if (s === "drafts" || n.indexOf("draft") >= 0) return "document-edit"
    if (s === "junk" || n.indexOf("junk") >= 0 || n.indexOf("spam") >= 0) return "mail-mark-junk"
    if (s === "all" || n.indexOf("all mail") >= 0) return "folder"
    return "folder"
}

function isPrimaryAccountFolderNorm(norm) {
    var n = (norm || "").toString().toLowerCase()
    return n === "inbox" || n === "[gmail]/sent mail" || n === "[gmail]/trash" || n === "[gmail]/drafts" || n === "[gmail]/spam" || n === "[gmail]/all mail"
}

// Build the main account folder list from the dataStore folders array.
// i18nFn: function(key) for translations — pass Qt i18n or a stub.
// inboxCategoryTabs: array from dataStore.inboxCategoryTabs
function accountFolderItems(folders, inboxCategoryTabs, i18nFn) {
    if (!folders || folders.length === 0)
        return []

    var bySpecialUse = {}
    var byNorm = {}

    for (var i = 0; i < folders.length; ++i) {
        var f = folders[i]
        var rawName = normalizedRemoteFolderName((f.name || "").toString())

        if (!rawName.length) continue
        if (isCategoryFolder(rawName)) continue
        if (isNoSelectFolder(f.flags)) continue

        var specialUse = (f.specialUse || "").toString().toLowerCase()

        if (!specialUse.length && rawName.toLowerCase() === "inbox")
            specialUse = "inbox"

        if (!specialUse.length) continue

        var norm = rawName.toLowerCase()
        if (byNorm[norm]) continue

        var dn = displayFolderName(rawName)
        if (specialUse === "junk") dn = i18nFn("Junk Email")

        var item = {
            key: "account:" + norm,
            name: dn,
            rawName: rawName,
            icon: iconForSpecialUse(specialUse, rawName),
            categories: [],
            specialUse: specialUse
        }
        byNorm[norm] = item

        if (!bySpecialUse[specialUse])
            bySpecialUse[specialUse] = item
    }

    var preferredUse = ["inbox", "sent", "trash", "drafts", "junk", "all"]
    var ordered = []
    var used = {}
    for (var p = 0; p < preferredUse.length; ++p) {
        var itm = bySpecialUse[preferredUse[p]]
        if (itm && !used[itm.rawName.toLowerCase()]) {
            ordered.push(itm)
            used[itm.rawName.toLowerCase()] = true
        }
    }
    var remaining = Object.keys(byNorm).sort()
    for (var r = 0; r < remaining.length; ++r) {
        if (!used[remaining[r]]) ordered.push(byNorm[remaining[r]])
    }

    // Attach Gmail category tabs to the inbox entry.
    var derived = inboxCategoryTabs || ["Primary"]
    for (var j = 0; j < ordered.length; ++j) {
        if ((ordered[j].specialUse || "") === "inbox") {
            var cats = []
            for (var c = 0; c < derived.length; ++c) {
                var key = (derived[c] || "").toString().toLowerCase()
                if (key === "primary") cats.push(i18nFn("Primary"))
                else if (key === "promotions") cats.push(i18nFn("Promotions"))
                else if (key === "social") cats.push(i18nFn("Social"))
            }
            if (cats.length === 0) cats.push(i18nFn("Primary"))
            ordered[j].categories = cats
            break
        }
    }

    return ordered
}

// Build the "More" subfolder items (hierarchical, non-primary, non-tag).
// primaryItems: result of accountFolderItems()
// isMoreFolderExpandedFn: function(path) -> bool
function moreAccountFolderItems(folders, primaryItems, isMoreFolderExpandedFn, i18nFn) {
    if (!folders || folders.length === 0) return []

    var primaryKeys = {}
    for (var i = 0; i < primaryItems.length; ++i)
        primaryKeys[(primaryItems[i].rawName || "").toString().toLowerCase()] = true

    var byNorm = {}
    for (var i2 = 0; i2 < folders.length; ++i2) {
        var f = folders[i2]
        var rawName = normalizedRemoteFolderName((f.name || "").toString())
        if (!rawName.length) continue
        if (isCategoryFolder(rawName)) continue
        if (rawName.indexOf("/") < 0) continue

        var norm = rawName.toLowerCase()
        if (primaryKeys[norm]) continue

        var parts = rawName.split("/")
        var level = Math.max(0, parts.length - 1)
        if (parts.length > 1) {
            var head = (parts[0] || "").toLowerCase()
            if (head === "[gmail]" || head === "[google mail]") level = Math.max(0, level - 1)
        }

        byNorm[norm] = {
            key: "account:" + norm,
            name: displayFolderName(rawName),
            rawName: rawName,
            icon: iconForSpecialUse(f.specialUse, rawName),
            categories: [],
            flags: (f.flags || "").toString(),
            noselect: isNoSelectFolder(f.flags),
            level: level
        }
    }

    // Synthesize missing parent nodes.
    var existing = Object.keys(byNorm)
    for (var i3 = 0; i3 < existing.length; ++i3) {
        var raw = byNorm[existing[i3]].rawName
        var pts = raw.split("/")
        if (pts.length < 2) continue

        var parentPath = pts[0]
        for (var p = 1; p < pts.length; ++p) {
            var parentNorm = parentPath.toLowerCase()
            if (!byNorm[parentNorm] && !primaryKeys[parentNorm]) {
                byNorm[parentNorm] = {
                    key: "account:" + parentNorm,
                    name: displayFolderName(parentPath),
                    rawName: parentPath,
                    icon: "folder",
                    categories: [],
                    flags: "\\Noselect (synthetic)",
                    noselect: true,
                    level: Math.max(0, parentPath.split("/").length - 1)
                }
            }
            parentPath += "/" + pts[p]
        }
    }

    var candidates = Object.keys(byNorm).map(function(k){ return byNorm[k] })
    candidates.sort(function(a, b) { return a.rawName.localeCompare(b.rawName) })

    var out = []
    for (var i4 = 0; i4 < candidates.length; ++i4) {
        var c = candidates[i4]
        var cNorm = c.rawName.toLowerCase()
        if (cNorm === "[gmail]" || cNorm === "[google mail]") continue
        var prefix = cNorm + "/"
        var hasChild = false
        for (var j = 0; j < candidates.length; ++j) {
            if (candidates[j].rawName.toLowerCase().indexOf(prefix) === 0) { hasChild = true; break }
        }
        if (c.noselect && !hasChild) continue

        var slash = c.rawName.lastIndexOf("/")
        var parentRaw = slash > 0 ? c.rawName.slice(0, slash) : ""

        var visible = true
        var walk = parentRaw
        while (walk.length) {
            if (!isMoreFolderExpandedFn(walk)) { visible = false; break }
            var s = walk.lastIndexOf("/")
            walk = s > 0 ? walk.slice(0, s) : ""
        }
        if (!visible) continue

        out.push({
            key: c.key,
            name: c.name,
            rawName: c.rawName,
            icon: c.icon,
            categories: c.categories,
            flags: c.flags,
            noselect: c.noselect,
            level: c.level,
            hasChildren: hasChild,
            expanded: isMoreFolderExpandedFn(c.rawName),
            parentRaw: parentRaw
        })
    }

    return out
}

// Build the tag folder items list.
// tagItemsFn: function() returning array from dataStore.tagItems()
// tagColorForNameFn: function(name, usedColors) returning color
function tagFolderItems(folders, tagItems, tagColorForNameFn) {
    var byKey = {}

    for (var i = 0; i < tagItems.length; ++i) {
        var t = tagItems[i]
        var label = (t.label || "").toString()
        if (!label.length) continue
        var norm = label.toLowerCase()
        byKey[norm] = {
            key: "tag:" + norm,
            name: displayFolderName((t.name || label).toString()),
            rawName: label,
            icon: "tag",
            categories: [],
            unread: Number(t.unread || 0),
            total: Number(t.total || 0),
            accentColor: tagColorForNameFn(norm)
        }
    }

    for (var i2 = 0; i2 < folders.length; ++i2) {
        var f = folders[i2]
        var rawName = normalizedRemoteFolderName((f.name || "").toString())
        if (!rawName.length) continue
        if (isCategoryFolder(rawName)) continue
        if (isNoSelectFolder(f.flags)) continue
        if ((f.specialUse || "").toString().length) continue
        if (rawName.indexOf("/") >= 0) continue
        var norm2 = rawName.toLowerCase()
        if (norm2 === "inbox") continue
        if (byKey[norm2]) continue
        byKey[norm2] = {
            key: "tag:" + norm2,
            name: displayFolderName(rawName),
            rawName: rawName,
            icon: "tag",
            categories: [],
            unread: 0,
            total: 0,
            accentColor: tagColorForNameFn(norm2)
        }
    }

    var out = Object.values(byKey)
    out.sort(function(a, b) { return a.name.localeCompare(b.name) })

    // Deterministic anti-collision pass for visible tags.
    var usedColors = []
    for (var i3 = 0; i3 < out.length; ++i3) {
        var item = out[i3]
        var norm3 = ((item.rawName || "").toString() || "").toLowerCase()
        item.accentColor = tagColorForNameFn(norm3, usedColors)
        usedColors.push(item.accentColor)
    }

    return out
}

// Build visible favorites from allFavoriteDefinitions and favoritesConfig.
function visibleFavoriteItems(allFavoriteDefinitions, favoritesConfig) {
    var enabledKeys = {}
    var count = 0
    for (var i = 0; i < favoritesConfig.length; ++i) {
        if (favoritesConfig[i].enabled) {
            enabledKeys[favoritesConfig[i].key] = true
            count++
        }
    }
    if (count === 0) {
        enabledKeys["all-inboxes"] = true
        enabledKeys["unread"] = true
        enabledKeys["flagged"] = true
    }
    var out = []
    for (var i2 = 0; i2 < allFavoriteDefinitions.length; ++i2) {
        var f = allFavoriteDefinitions[i2]
        if (enabledKeys[f.key])
            out.push({ key: "favorites:" + f.key, name: f.name, icon: f.icon, categories: [] })
    }
    return out
}

// Build the local folder items list.
// defaultLocalFolderDefs: array of { key, name, icon }
// userFolders: array from dataStore.userFolders()
function allLocalFolderItems(defaultLocalFolderDefs, userFolders) {
    var out = defaultLocalFolderDefs.map(function(f) {
        return { key: f.key, name: f.name, icon: f.icon, categories: [] }
    })
    for (var i = 0; i < userFolders.length; ++i) {
        var n = (userFolders[i].name || "").toString()
        if (!n.length) continue
        var k = n.toLowerCase().replace(/\s+/g, "-")
        out.push({ key: "local:" + k, name: n, icon: "folder", categories: [] })
    }
    return out
}

function folderEntryByKey(key, favoriteItems, tagItems, accountItems, moreItems, localItems) {
    var groups = [favoriteItems, tagItems, accountItems, moreItems, localItems]
    for (var g = 0; g < groups.length; ++g) {
        var items = groups[g]
        for (var i = 0; i < items.length; ++i) {
            if (items[i].key === key) return items[i]
        }
    }
    return null
}

function normalizedFolderFromKey(key) {
    if (!key || key.indexOf("account:") !== 0) return ""
    return key.slice("account:".length).toLowerCase()
}

function selectedImapFolderName(selectedFolderKey, folders) {
    if (selectedFolderKey.indexOf("account:") !== 0) return ""
    var target = normalizedFolderFromKey(selectedFolderKey)
    for (var i = 0; i < folders.length; ++i) {
        var raw = (folders[i].name || "").toString()
        if (raw.toLowerCase() === target) return raw
    }
    return target.length ? target : ""
}

function folderStatsByKey(dataStoreObj, folderKey, rawFolderName) {
    if (!dataStoreObj || !dataStoreObj.statsForFolder) return { total: 0, unread: 0 }
    return dataStoreObj.statsForFolder(folderKey || "", rawFolderName || "")
}

function folderTooltipText(displayName, folderKey, rawFolderName, dataStoreObj) {
    var s = folderStatsByKey(dataStoreObj, folderKey, rawFolderName)
    return displayName + " - " + s.total + " items (" + s.unread + " unread)"
}
