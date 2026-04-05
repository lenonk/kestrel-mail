.pragma library

function trashFolderForHost(imapHost) {
    var h = (imapHost || "").toLowerCase()
    if (h.indexOf("gmail") >= 0 || h.indexOf("googlemail") >= 0)
        return "[Gmail]/Trash"
    return "Trash"
}

function calendarWeekBoundsIso() {
    var now = new Date()
    var start = new Date(now)
    start.setHours(0, 0, 0, 0)
    var mondayOffset = (start.getDay() + 6) % 7
    start.setDate(start.getDate() - mondayOffset)
    var end = new Date(start)
    end.setDate(end.getDate() + 7)
    return { startIso: start.toISOString(), endIso: end.toISOString() }
}

// Build the visible calendar source IDs array from calendarSources.
function visibleCalendarSourceIds(calendarSources) {
    var out = []
    for (var i = 0; i < calendarSources.length; ++i) {
        var c = calendarSources[i]
        if (c && c.checked) out.push(String(c.id || ""))
    }
    return out
}

// Rebuild calendarSources from incoming Google calendar list.
// existing: current calendarSources array
// incoming: imapServiceObj.googleCalendarList
// primaryEmail: lowercased primary account email
// calendarI18n: i18n("Calendar") text
function rebuildCalendarSourcesFromGoogle(existing, incoming, primaryEmail, calendarI18n) {
    if (!incoming || incoming.length === 0)
        return existing

    var checkedMap = {}
    for (var i = 0; i < existing.length; ++i) {
        var c = existing[i]
        checkedMap[String(c.id || "")] = !!c.checked
    }

    var next = []
    for (var i2 = 0; i2 < incoming.length; ++i2) {
        var g = incoming[i2]
        var id = String(g.id || "")
        var rawName = String(g.name || g.summary || id)
        var displayName = (id.toLowerCase() === primaryEmail) ? calendarI18n : rawName
        next.push({
            account: "gmail",
            id: id,
            name: displayName,
            checked: checkedMap.hasOwnProperty(id) ? checkedMap[id] : true,
            color: String(g.color || g.backgroundColor || "")
        })
    }

    next.sort(function(a, b) {
        var aIsAccount = (a.id.toLowerCase() === primaryEmail) ? 0 : 1
        var bIsAccount = (b.id.toLowerCase() === primaryEmail) ? 0 : 1
        if (aIsAccount !== bIsAccount) return aIsAccount - bIsAccount
        return a.name.localeCompare(b.name)
    })

    return next
}

// Update a single calendar source's checked state. Returns new array.
function setCalendarSourceChecked(calendarSources, sourceId, checked) {
    var next = []
    for (var i = 0; i < calendarSources.length; ++i) {
        var c = calendarSources[i]
        if (String(c.id) === String(sourceId))
            next.push({ account: c.account, id: c.id, name: c.name, checked: !!checked, color: c.color })
        else
            next.push(c)
    }
    return next
}
