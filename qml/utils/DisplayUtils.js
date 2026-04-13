.pragma library

function bucketKeyForDate(dateValue) {
    var d = new Date(dateValue)
    if (isNaN(d.getTime())) return "older"

    var now = new Date()
    var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate())
    var targetStart = new Date(d.getFullYear(), d.getMonth(), d.getDate())
    var dayMs = 24 * 60 * 60 * 1000
    var diffDays = Math.floor((todayStart.getTime() - targetStart.getTime()) / dayMs)

    if (diffDays <= 0) return "today"
    if (diffDays === 1) return "yesterday"

    var weekStart = new Date(todayStart)
    weekStart.setDate(todayStart.getDate() - todayStart.getDay())
    if (targetStart >= weekStart && targetStart < todayStart) {
        var dow = targetStart.getDay()
        return "weekday-" + (dow === 0 ? 7 : dow)
    }

    if (diffDays <= 14) return "lastWeek"
    if (diffDays <= 21) return "twoWeeksAgo"
    return "older"
}

function senderEmail(senderValue) {
    var s = (senderValue || "").toString()
    var m = s.match(/([A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,})/i)
    return m && m[1] ? m[1].toLowerCase() : ""
}

function senderDomain(senderValue) {
    var s = (senderValue || "").toString()
    var m = s.match(/[A-Z0-9._%+-]+@([A-Z0-9.-]+\.[A-Z]{2,})/i)
    if (!m || !m[1]) return ""
    var full = m[1].toLowerCase()
    var parts = full.split('.')
    if (parts.length <= 2) return full

    var tail2 = parts.slice(-2).join('.')
    var cc2 = ["co.uk", "com.au", "co.jp", "com.br", "com.mx"]
    var tail3 = parts.slice(-3).join('.')
    return cc2.indexOf(tail2) >= 0 ? tail3 : tail2
}

function _displayNameFromMailbox(rawValue, fallbackText, senderEmailFn) {
    var s = (rawValue || "").toString().trim()
    if (!s.length) return fallbackText
    var comma = s.indexOf(",")
    if (comma > 0) s = s.slice(0, comma).trim()
    var lt = s.indexOf("<")
    if (lt > 0) s = s.slice(0, lt).trim()
    s = s.replace(/<[^>]*>/g, " ")
    s = s.replace(/(^|\s)[A-Za-z]+>/g, " ")
    s = s.replace(/&[A-Za-z0-9#]+;/g, " ")
    s = s.replace(/["']/g, "")
    s = s.replace(/\s+/g, " ").trim()
    if (!s.length) {
        var email = senderEmailFn(rawValue)
        if (email.length) s = email.split("@")[0]
    }
    if (/^[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}$/i.test(s)) {
        s = s.split("@")[0]
    }
    return s.length ? s : fallbackText
}

function resolvedMailboxEmail(rawValue, accountEmailHint) {
    var raw = (rawValue || "").toString().trim()
    var parsed = senderEmail(raw)
    if (parsed.length) return parsed

    var token = raw.toLowerCase()
    var account = (accountEmailHint || "").toString().trim().toLowerCase()
    var at = account.indexOf("@")
    if (at > 0 && token.length && token === account.slice(0, at)) {
        return account
    }
    return ""
}

// Extract the display name embedded in a "Name <email>" value.
// Returns "" when the raw value is bare email or empty.
// Flips "Last, First" → "First Last" (corporate LDAP convention).
function _inlineDisplayName(rawValue) {
    var s = (rawValue || "").toString().trim()
    if (!s.length) return ""
    var lt = s.indexOf("<")
    if (lt > 0) s = s.slice(0, lt).trim()
    s = s.replace(/["']/g, "").trim()
    if (!s.length) return ""
    if (/^[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}$/i.test(s)) return ""
    var parts = s.split(",")
    if (parts.length === 2) {
        var first = parts[1].trim()
        var last = parts[0].trim()
        if (first.length && last.length) s = first + " " + last
    }
    return s
}

// dataStoreObj may be null; if non-null, uses displayNameForEmail.
// Prefers the per-message inline name so shared addresses (e.g.
// notifications@github.com) show the correct sender per message.
function displayNameForAddress(rawAddressValue, accountEmailHint, dataStoreObj) {
    var raw = (rawAddressValue || "").toString().trim()

    // Prefer the name embedded in this specific message header.
    var inline = _inlineDisplayName(raw)
    if (inline.length) return inline

    // Fall back to the contact store when the raw value is a bare email.
    var email = resolvedMailboxEmail(raw, accountEmailHint)
    if (email.length) {
        if (dataStoreObj && dataStoreObj.displayNameForEmail) {
            var known = (dataStoreObj.displayNameForEmail(email) || "").toString().trim()
            if (known.length) return known
        }
        return email
    }
    return ""
}

function displaySenderName(senderValue, accountEmailHint, dataStoreObj, unknownSenderText) {
    var resolved = displayNameForAddress(senderValue, accountEmailHint, dataStoreObj)
    if (resolved.length) return resolved
    var raw = (senderValue || "").toString().trim()
    return raw.length ? raw : (unknownSenderText || "Unknown sender")
}

// Returns a comma-separated string of unique participant display names for a thread.
// accountEmails: array of account email strings (lowercased)
// meText: i18n("Me")
function displayThreadParticipants(allSendersRaw, accountEmailHint, accountEmails, dataStoreObj, meText) {
    var raw = (allSendersRaw || "").toString()
    if (!raw.length) return ""

    var myEmails = accountEmails || []

    var parts = raw.split("\x1f")
    var seen = {}
    var others = []
    var hasSelf = false

    for (var i = 0; i < parts.length; ++i) {
        var sender = (parts[i] || "").trim()
        if (!sender.length) continue

        var m = sender.match(/<([^>]+)>/)
        var email = (m ? m[1] : sender).trim().toLowerCase()

        var isSelf = false
        for (var j = 0; j < myEmails.length; ++j) {
            if (myEmails[j] === email) { isSelf = true; break }
        }
        if (isSelf) {
            hasSelf = true
            continue
        }

        var label = displaySenderName(sender, accountEmailHint, dataStoreObj)
        var key = label.toLowerCase()
        if (!seen[key]) {
            seen[key] = true
            others.push(label)
        }
    }

    if (hasSelf) others.push(meText || "Me")
    return others.join(", ")
}

function _titleCaseWords(s) {
    var raw = (s || "").toString().trim()
    if (!raw.length) return ""

    var hasLower = /[a-z]/.test(raw)
    var hasUpper = /[A-Z]/.test(raw)
    if (!(hasUpper && !hasLower)) {
        return raw.replace(/\s+/g, " ").trim()
    }

    var parts = raw.toLowerCase().split(/\s+/)
    return parts.map(function(p) { return p.length ? (p.charAt(0).toUpperCase() + p.slice(1)) : "" }).join(" ").trim()
}

function _accountDisplayNameFromEmail(email) {
    var e = (email || "").toString().trim().toLowerCase()
    var at = e.indexOf("@")
    if (at <= 0) return ""
    var local = e.slice(0, at).replace(/[._-]+/g, " ").trim()
    var parts = local.split(/\s+/)
    return parts.map(function(p) { return p.length ? (p.charAt(0).toUpperCase() + p.slice(1)) : "" }).join(" ").trim()
}

function displayRecipientName(recipientValue, accountEmailHint, dataStoreObj, recipientText) {
    var resolved = displayNameForAddress(recipientValue, accountEmailHint, dataStoreObj)
    if (resolved.length) return resolved
    var raw = (recipientValue || "").toString().trim()
    return raw.length ? raw : (recipientText || "Recipient")
}

function _splitRecipientMailboxes(recipientValue) {
    var raw = (recipientValue || "").toString().trim()
    if (!raw.length) return []
    var parts = raw.split(/,(?=(?:[^"]*"[^"]*")*[^"]*$)/)
    return parts.map(function(p) { return (p || "").trim() }).filter(function(p) { return p.length > 0 })
}

function displayRecipientNames(recipientValue, accountEmailHint, dataStoreObj, recipientText) {
    var mailboxes = _splitRecipientMailboxes(recipientValue)
    if (!mailboxes.length) return displayRecipientName(recipientValue, accountEmailHint, dataStoreObj, recipientText)
    var labels = []
    for (var i = 0; i < mailboxes.length; ++i) {
        var label = displayRecipientName(mailboxes[i], accountEmailHint, dataStoreObj, recipientText)
        if (label.length) labels.push(label)
    }
    return labels.length ? labels.join(", ") : displayRecipientName(recipientValue, accountEmailHint, dataStoreObj, recipientText)
}

function formatListDate(dateValue, localeFn) {
    var d = new Date(dateValue)
    if (isNaN(d.getTime())) return ""
    var bucket = bucketKeyForDate(dateValue)
    if (bucket === "today") return localeFn(d, "h:mm AP")
    if (bucket === "yesterday" || bucket.startsWith("weekday-") || bucket === "lastWeek") return localeFn(d, "ddd h:mm AP")
    return localeFn(d, "ddd M/d")
}

function formatContentDate(dateValue, localeFn) {
    var d = new Date(dateValue)
    if (isNaN(d.getTime())) return ""
    var bucket = bucketKeyForDate(dateValue)
    if (bucket === "today") return localeFn(d, "h:mm AP")
    return localeFn(d, "ddd M/d/yyyy h:mm AP")
}

function avatarInitials(nameValue) {
    var s = (nameValue || "").toString().trim()
    if (!s.length) return "?"
    var parts = s.split(/\s+/).filter(function(p) { return p.length > 0 })
    if (parts.length >= 2) {
        return (parts[0].charAt(0) + parts[1].charAt(0)).toUpperCase()
    }
    return parts[0].charAt(0).toUpperCase()
}

function emphasizeSubjectEmoji(subjectValue) {
    var s = (subjectValue || "").toString()
    var escaped = s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    return escaped.replace(/([\uD800-\uDBFF][\uDC00-\uDFFF])/g, '<span style="font-size:20px;">$1</span>')
}

function subjectRichText(subjectValue, noSubjectText) {
    var s = (subjectValue || (noSubjectText || "(No subject)")).toString()
    var escaped = s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    return escaped.replace(/[\uD800-\uDBFF][\uDC00-\uDFFF]|[\u2600-\u27BF]/g,
                           function(m) { return "<span style='font-size:18px'>" + m + "</span>" })
}

function parseMessageKey(key) {
    var s = (key || "").toString()
    var prefix = "msg:"
    if (s.indexOf(prefix) !== 0) return null
    var parts = s.slice(prefix.length).split("|")
    if (parts.length < 3) return null
    return { accountEmail: parts[0], folder: parts[1], uid: parts[2] }
}

function isBodyHtmlUsable(value) {
    var html = (value || "").toString().trim()
    if (!html.length) return false
    var lower = html.toLowerCase()
    if (lower.indexOf("ok success [throttled]") >= 0) return false
    if (lower.indexOf("authenticationfailed") >= 0) return false
    if (lower.indexOf("no-fetch-payload") >= 0) return false
    if (/\bsrc\s*=\s*["']\s*cid:/i.test(html)) return false
    var hasHtmlish = /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html)
    return hasHtmlish
}

function senderAvatarSources(senderValue, accountEmailHint, dataStoreObj) {
    var email = senderEmail(senderValue)
    var accountEmail = (accountEmailHint || "").toString().trim().toLowerCase()
    if (!email.length || (accountEmail.length && email === accountEmail))
        return []
    if (!dataStoreObj || !dataStoreObj.avatarForEmail)
        return []
    var url = (dataStoreObj.avatarForEmail(email) || "").toString().trim()
    return url.length ? [url] : []
}

function avatarSourceLabel(urlValue) {
    var u = (urlValue || "").toString().toLowerCase()
    if (!u.length) return "initials"
    if (u.indexOf("people.googleapis.com") >= 0 || u.indexOf("googleusercontent.com") >= 0) return "google-people"
    if (u.indexOf("_bimi") >= 0 || u.indexOf("/bimi/") >= 0 || u.indexOf("vmc.") >= 0) return "bimi"
    if (u.indexOf("google.com/s2/favicons") >= 0) return "favicon"
    if (u.indexOf("gravatar.com/avatar") >= 0) return "gravatar"
    return "custom"
}
