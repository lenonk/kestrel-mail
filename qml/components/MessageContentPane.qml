import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    required property var appRoot
    required property var systemPalette

    property bool forceDarkHtml: !!(appRoot ? appRoot.contentPaneDarkModeEnabled : true)
    property var tagMap: ({})
    property bool showHeaderMeta: true

    readonly property var messageData: appRoot.selectedMessageData
    readonly property string senderText: messageData && messageData.sender ? messageData.sender : i18n("Unknown sender")
    readonly property string senderName: appRoot.displaySenderName(senderText, messageData && messageData.accountEmail ? messageData.accountEmail : "")
    readonly property string senderEmail: appRoot.senderEmail(senderText)
    readonly property string recipientsText: (messageData && messageData.recipient && messageData.recipient.toString().trim().length > 0)
                                             ? messageData.recipient
                                             : ((messageData && messageData.accountEmail) ? messageData.accountEmail : i18n("No recipient"))
    readonly property string recipientName: appRoot.displayRecipientNames(recipientsText, messageData && messageData.accountEmail ? messageData.accountEmail : "")
    readonly property var recipientAvatarSources: appRoot.senderAvatarSources(
                                                  recipientsText,
                                                  "",
                                                  "",
                                                  messageData && messageData.accountEmail ? messageData.accountEmail : "")
    readonly property string folderName: i18n("Inbox")
    readonly property string messageSubject: (messageData && messageData.subject) ? messageData.subject : i18n("(No subject)")
    readonly property string receivedAtText: appRoot.formatContentDate(messageData ? messageData.receivedAt : "")
    readonly property string messageBodyHtml: (messageData && messageData.bodyHtml) ? messageData.bodyHtml : ""
    readonly property bool hasUsableBodyHtml: {
        const html = (messageBodyHtml || "").toString().trim()
        if (!html.length) return false
        const lower = html.toLowerCase()
        if (lower.indexOf("ok success [throttled]") >= 0) return false
        if (lower.indexOf("authenticationfailed") >= 0) return false
        return /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html)
    }
    readonly property string renderedHtml: {
        if (!root.hasUsableBodyHtml) return ""
        const base = root.htmlForMessage()
        let sanitized = root.sanitizeRenderHtml(base)
        // Neutralize tracking pixels unless the user has explicitly allowed them.
        // Reading root.trackingAllowed here makes this binding reactive to that property.
        if (!root.trackingAllowed) sanitized = root.neutralizeTrackingPixels(sanitized)
        // Inject a baseline background so plain-text and unstyled messages use the
        // theme background. No !important — emails with their own explicit backgrounds
        // will still override this. Reading the property here keeps the binding reactive.
        const bgColor = Kirigami.Theme.backgroundColor.toString()
        const bgStyle = "<style data-kestrel-bg='baseline'>html,body{background-color:" + bgColor + ";}</style>"
        if (sanitized.indexOf("<head>") >= 0)
            sanitized = sanitized.replace("<head>", "<head>" + bgStyle)
        return root.forceDarkHtml ? root.darkenHtml(sanitized) : sanitized
    }
    readonly property var activeTags: {
        const key = appRoot.selectedMessageKey || ""
        if (!key.length) return []
        return tagMap[key] ? tagMap[key] : []
    }
    readonly property bool immersiveBodyMode: !!(appRoot && appRoot.contentPaneHoverExpandActive)

    property bool imagesAllowed: false
    property bool trackingAllowed: false

    // Read domain directly from messageData.sender to avoid transient binding lag
    // through senderText/senderEmail during selection updates.
    readonly property string senderDomain: {
        const senderVal = (root.messageData && root.messageData.sender)
                          ? root.messageData.sender.toString() : ""
        if (!senderVal)
            return ""

        let email = senderVal
        const angleMatch = senderVal.match(/<([^>]+@[^>]+)>/)
        if (angleMatch)
            email = angleMatch[1]

        const at = email.lastIndexOf('@')
        return at >= 0 ? email.slice(at + 1).trim().toLowerCase() : ""
    }
    // Reads directly from messageData.sender so it updates atomically with the message,
    // avoiding the senderDomain → senderEmail → senderText binding chain lag.
    readonly property bool isTrustedSender: {
        const senderVal = (root.messageData && root.messageData.sender)
                          ? root.messageData.sender.toString() : ""
        if (!senderVal)
            return false

        let email = senderVal

        const angleMatch = senderVal.match(/<([^>]+@[^>]+)>/)
        if (angleMatch)
            email = angleMatch[1]

        const at = email.lastIndexOf('@')
        const domain = at >= 0 ? email.slice(at + 1).trim().toLowerCase() : ""

        return !!(domain && appRoot && appRoot.dataStoreObj
                  && appRoot.dataStoreObj.isSenderTrusted(domain))
    }

    readonly property bool hasExternalImages: {
        const html = (root.messageBodyHtml || "").toString()
        return /src\s*=\s*["']https?:\/\//i.test(html)
    }

    function _fileNameFromUrl(url) {
        const raw = (url || "").toString()
        if (!raw.length) return i18n("Attachment")
        const noQuery = raw.split("?")[0]
        const parts = noQuery.split("/")
        const last = parts.length ? parts[parts.length - 1] : ""
        return last.length ? decodeURIComponent(last) : i18n("Attachment")
    }

    function extractAttachmentItemsFromHtml(html) {
        const out = []
        const src = (html || "").toString()
        if (!src.length) return out

        const seen = {}

        function maybeAdd(url, labelHint) {
            const href = (url || "").trim()
            if (!/^https?:\/\//i.test(href))
                return
            if (seen[href])
                return

            const lower = href.toLowerCase()
            const hasFileExt = /\.(pdf|png|jpe?g|gif|webp|heic|docx?|xlsx?|pptx?|zip|rar|7z|txt|csv)($|\?)/i.test(lower)
            const dotloopDoc = /dotloop\.com\/.+\/document\?/i.test(lower) || /[?&]documentid=/i.test(lower)
            const genericAttachment = /download|attachment|file=/i.test(lower)

            if (!(hasFileExt || dotloopDoc || genericAttachment))
                return

            seen[href] = true

            let label = (labelHint || "").replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim()
            if (!label.length || /^https?:\/\//i.test(label)) {
                if (dotloopDoc)
                    label = i18n("Document")
                else
                    label = root._fileNameFromUrl(href)
            }

            out.push({
                name: label,
                url: href,
                canPreview: /\.(png|jpe?g|gif|webp)($|\?)/i.test(lower)
            })
        }

        // 1) Rich anchor links
        const anchorRe = /<a\b[^>]*href\s*=\s*["']([^"']+)["'][^>]*>([\s\S]*?)<\/a>/gi
        let m
        while ((m = anchorRe.exec(src)) !== null)
            maybeAdd(m[1], m[2])

        // 2) Generic href-bearing tags (some templates put navigable targets on non-<a> tags)
        const hrefRe = /\bhref\s*=\s*["']([^"']+)["']/gi
        while ((m = hrefRe.exec(src)) !== null)
            maybeAdd(m[1], "")

        return out
    }

    readonly property var attachmentItems: extractAttachmentItemsFromHtml(root.messageBodyHtml)

    // Tracking pixel detection: scans for 1×1 external <img> tags to confirm tracking is
    // present, then determines the vendor using headers before falling back to the pixel URL.
    // Priority: espVendor (C++ pre-computed from X-Mailgun-Sid / Received chain / etc.) →
    //           X-Mailer first word → pixel URL SLD → Return-Path domain → DKIM domain.
    readonly property var _trackerInfo: {
        const html        = (root.messageBodyHtml || "").toString()
        const espVendor   = root.messageData ? (root.messageData.espVendor   || "").toString() : ""
        const xMailer     = root.messageData ? (root.messageData.xMailer     || "").toString() : ""
        const returnPath  = root.messageData ? (root.messageData.returnPath  || "").toString() : ""
        const authResults = root.messageData ? (root.messageData.authResults || "").toString() : ""

        // Scan for a 1×1 external tracking pixel — this is the trigger for the whole bar.
        // First-party pixels (sender's own domain) are skipped UNLESS a definitive ESP header
        // (X-Mailgun-Sid, X-SG-EID, etc.) is present: many ESPs use custom CNAME tracking
        // domains that look first-party but route through the ESP's infrastructure.
        const senderDom = root.senderDomain || ""
        const tagRe = /<img\b[^>]*>/gi
        let pixelSrc = ""
        let m
        while ((m = tagRe.exec(html)) !== null) {
            const tag = m[0]
            if (!/\bsrc\s*=\s*["']https?:/i.test(tag)) continue
            if (!/\bwidth\s*=\s*["']\s*1\s*["']/i.test(tag)) continue
            if (!/\bheight\s*=\s*["']\s*1\s*["']/i.test(tag)) continue
            const srcM = tag.match(/\bsrc\s*=\s*["']([^"']+)/i)
            if (!srcM) continue
            const candidateSrc = srcM[1]
            if (isFirstPartyUrl(candidateSrc, senderDom)) {
                console.log("[tracking] skipped first-party pixel src=" + candidateSrc.substring(0, 80))
                continue
            }
            pixelSrc = candidateSrc
            break
        }
        if (!pixelSrc) return ({ found: false, vendor: "" })

        // Determine vendor from best available signal (richest first).
        const vendor = espVendor
                    || vendorFromXMailer(xMailer)
                    || vendorFromUrl(pixelSrc)
                    || vendorFromReturnPath(returnPath)
                    || vendorFromAuthResults(authResults)
        return ({ found: true, vendor })
    }
    readonly property bool hasTrackingPixel: root._trackerInfo.found
    readonly property string trackerVendor: root._trackerInfo.vendor
    readonly property string listUnsubscribeUrl: {
        return (root.messageData && root.messageData.listUnsubscribe)
               ? root.messageData.listUnsubscribe.toString().trim()
               : ""
    }

    onMessageDataChanged: {
        // Read messageData directly — derived bindings (isTrustedSender, senderDomain, etc.)
        // may not have re-evaluated yet when this handler fires.
        const senderVal = (messageData && messageData.sender) ? messageData.sender.toString() : ""
        let email = senderVal
        const angleMatch = senderVal.match(/<([^>]+@[^>]+)>/)
        if (angleMatch) email = angleMatch[1]
        const at = email.lastIndexOf('@')
        const domain = at >= 0 ? email.slice(at + 1).trim().toLowerCase() : ""

        const explicitTrust = !!(domain && appRoot && appRoot.dataStoreObj
                                  && appRoot.dataStoreObj.isSenderTrusted(domain))

        // Secondary trust signal: DKIM pass in Authentication-Results means the From domain
        // signed the message and it wasn't tampered with in transit.
        const authStr = (messageData && messageData.authResults)
                        ? messageData.authResults.toString() : ""
        const dkimPass = /\bdkim=pass\b/i.test(authStr)
        const dkimDomainMatch = authStr.match(/dkim=pass[^\n]*?header\.(?:d|i)=@?([a-z0-9._-]+)/i)
        const dkimDomain = dkimDomainMatch ? dkimDomainMatch[1] : ""

        if (explicitTrust) {
            imagesAllowed = true
            console.log("[images] autoload sender=" + domain + " reason=explicit-whitelist")
        } else if (dkimPass) {
            imagesAllowed = true
            console.log("[images] autoload sender=" + domain
                        + " reason=dkim-pass signing-domain=" + (dkimDomain || "unknown"))
        } else {
            imagesAllowed = false
            console.log("[images] blocked sender=" + (domain || "(unknown)")
                        + " reason=not-in-whitelist dkim-pass=" + dkimPass)
        }

        trackingAllowed = false
    }

    function setCurrentTags(tags) {
        const key = appRoot.selectedMessageKey || ""
        if (!key.length) return
        const next = Object.assign({}, tagMap)
        next[key] = tags
        tagMap = next
    }

    function removeTagByName(name) {
        const tags = activeTags.filter(function(t) { return t.name !== name })
        setCurrentTags(tags)
    }

    function addTag(tagObj) {
        const exists = activeTags.some(function(t) { return t.name === tagObj.name })
        if (exists) return
        const tags = activeTags.slice()
        tags.push(tagObj)
        setCurrentTags(tags)
    }

    function escapeHtml(text) {
        return (text || "").toString()
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
    }

    // Extract just the second-level domain name from a URL and capitalize it.
    // e.g. "https://opens.mailgun.org/..." → "Mailgun"
    // e.g. "https://images.mailgun.com/..." → "Mailgun"  (not "Images")
    function vendorFromUrl(url) {
        const m = (url || "").match(/^https?:\/\/([^/?#]+)/i)
        if (!m) return ""
        const parts = m[1].split(".")
        if (parts.length < 2) return ""
        const sld = parts[parts.length - 2]
        return sld.charAt(0).toUpperCase() + sld.slice(1)
    }

    // Extract ESP name from X-Mailer header (first alphabetic word only).
    // e.g. "Mailchimp Mailer" → "Mailchimp", "Klaviyo" → "Klaviyo"
    function vendorFromXMailer(xMailer) {
        const first = ((xMailer || "").trim().match(/^([A-Za-z][A-Za-z0-9]*)/) || [])[1] || ""
        return first ? first.charAt(0).toUpperCase() + first.slice(1) : ""
    }

    // Extract bounce domain from Return-Path (e.g. "<bounce@mg.mailchimp.com>") and
    // return its SLD. Falls back to treating the whole value as a domain string.
    function vendorFromReturnPath(returnPath) {
        if (!returnPath) return ""
        const m = returnPath.match(/<[^@>]*@([^>]+)>/)
        const domain = m ? m[1].trim() : returnPath.replace(/[<>\s]/g, "")
        return domain ? vendorFromUrl("https://" + domain) : ""
    }

    // Parse the DKIM signing domain from an Authentication-Results header and return its SLD.
    // Handles both "header.d=example.com" and "header.i=@example.com" forms.
    function vendorFromAuthResults(authResults) {
        if (!authResults) return ""
        const m = (authResults).match(/dkim=pass[^\n]*?header\.(?:d|i)=@?([a-z0-9._-]+)/i)
        return m ? vendorFromUrl("https://" + m[1]) : ""
    }

    // Returns true if the pixel URL belongs to the sender's own domain (first-party tracking).
    // Compares second-level domain labels: "m.aliexpress.com" vs sender "aliexpress.com" → true.
    // eM Client does not flag first-party pixels; we match that behaviour.
    function isFirstPartyUrl(url, senderDomain) {
        if (!url || !senderDomain) return false
        const urlM = (url || "").match(/^https?:\/\/([^/?#]+)/i)
        if (!urlM) return false
        const urlParts = urlM[1].split(".")
        if (urlParts.length < 2) return false
        const urlSld = urlParts[urlParts.length - 2].toLowerCase()
        const senderParts = senderDomain.toLowerCase().split(".")
        if (senderParts.length < 2) return false
        const senderSld = senderParts[senderParts.length - 2]
        return urlSld === senderSld
    }

    // If a URL contains a query param whose value is itself a full HTTP URL, return that value.
    // No domain allowlist — any URL with redirect=https://... is a redirect and should be unwrapped.
    function extractTrackingRedirectUrl(href) {
        const m = /[?&](?:redirect|url|to|link|target|dest|goto)=(https?(?:%3A%2F%2F|:\/\/)[^&]*)/i.exec(href)
        if (m) { try { return decodeURIComponent(m[1]) } catch(e) { return m[1] } }
        return null
    }

    // Replace the src of any 1×1 external third-party tracking pixel with a transparent data
    // URI so the beacon never fires, even when autoLoadImages is true. Uses the same detection
    // criteria as _trackerInfo (including first-party skip). The original src is preserved in
    // data-tracking-src for later restore.
    function neutralizeTrackingPixels(html) {
        const blank = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"
        const senderDom = root.senderDomain || ""
        let count = 0
        const result = html.replace(/<img\b[^>]*>/gi, function(tag) {
            if (!/\bsrc\s*=\s*["']https?:/i.test(tag))          return tag
            if (!/\bwidth\s*=\s*["']\s*1\s*["']/i.test(tag))   return tag
            if (!/\bheight\s*=\s*["']\s*1\s*["']/i.test(tag))  return tag
            const srcM = tag.match(/\bsrc\s*=\s*["']([^"']+)/i)
            const src = srcM ? srcM[1] : "(unknown)"
            if (isFirstPartyUrl(src, senderDom)) {
                console.log("[tracking] neutralize skipped first-party pixel src=" + src.substring(0, 80))
                return tag
            }
            const vendor = root.trackerVendor || "unknown"
            console.log("[tracking] neutralized pixel #" + (++count)
                        + " vendor=" + vendor + " src=" + src.substring(0, 120))
            return tag.replace(/(\bsrc\s*=\s*(["']))([^"']+)\2/i, '$1' + blank + '$2')
        })
        if (count === 0)
            console.log("[tracking] neutralizeTrackingPixels called but no 1×1 pixels found")
        return result
    }

    // Replace known tracking redirect hrefs with their final destination URLs.
    function sanitizeTrackingLinks(html) {
        return html.replace(/(<a\b[^>]*\bhref\s*=\s*)(["'])(https?:\/\/[^"']{20,})\2/gi,
            function(match, pre, quote, href) {
                const dest = extractTrackingRedirectUrl(href)
                if (dest) {
                    console.log("[link-sanitize]", href.substring(0, 100), "→", dest.substring(0, 100))
                    return pre + quote + dest + quote
                }
                return match
            })
    }

    function htmlForMessage() {
        if (messageBodyHtml && messageBodyHtml.trim().length > 0) {
            return messageBodyHtml
        }
        return ""
    }

    function decodeQuotedPrintableText(input) {
        let s = (input || "").toString()
        if (!s.length) return s

        // Soft line breaks.
        s = s.replace(/=\r?\n/g, "")

        // Hex escapes (=3D, =0A, ...).
        s = s.replace(/=([0-9A-Fa-f]{2})/g, function(_, hex) {
            const code = parseInt(hex, 16)
            if (isNaN(code)) return _
            return String.fromCharCode(code)
        })

        return s
    }

    function sanitizeRenderHtml(rawHtml) {
        let html = (rawHtml || "").toString()
        if (!html.length) return "<html><body></body></html>"

        html = html.replace(/\r\n/g, "\n")

        // Remove binary/control garbage that can break WebEngine parsing.
        html = html.replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F]/g, "")

        // Strip external scripts: prevents blocking DOMContentLoaded (e.g. tracking/analytics
        // scripts that time out) and is correct security hygiene for email HTML.
        html = html.replace(/<script\b[^>]*\bsrc\s*=[^>]*>[\s\S]*?<\/script>/gi, "")

        // Strip external stylesheet links (especially web fonts) to avoid render stalls
        // waiting on third-party CSS/font hosts.
        html = html.replace(/<link\b[^>]*\brel\s*=\s*["']?stylesheet["']?[^>]*>/gi, "")

        // Rewrite known tracking redirect links to their real destination URLs.
        html = sanitizeTrackingLinks(html)

        // If we already have a full HTML document, avoid destructive rewrites.
        const trimmed = html.trim()
        if (/<html\b/i.test(trimmed) && /<\/html>\s*$/i.test(trimmed)) {
            return trimmed
        }

        // If we got MIME-ish payload, cut to content after first header break.
        const mimeHeaderLike = /^(mime-version:|content-type:|content-transfer-encoding:|x-[a-z0-9-]+:)/im.test(html)
        if (mimeHeaderLike) {
            const splitAt = html.search(/\n\n/)
            if (splitAt >= 0 && splitAt < html.length - 2) {
                html = html.slice(splitAt + 2)
            }
        }

        // Drop obvious IMAP protocol lines that occasionally leak into payload.
        html = html
                .split("\n")
                .filter(function(line) {
                    const t = line.trim()
                    if (!t.length) return true
                    if (/^\*\s+\d+\s+fetch\s*\(/i.test(t)) return false
                    if (/^[a-z]\d+\s+(ok|no|bad)\b/i.test(t)) return false
                    if (/^body\[header\.fields/i.test(t)) return false
                    return true
                })
                .join("\n")

        // Decode quoted-printable artifacts when present.
        if (/=\r?\n|=[0-9A-Fa-f]{2}/.test(html)) {
            html = decodeQuotedPrintableText(html)
        }

        // Strip MIME preamble lines that sometimes leak into body content.
        const lines = html.split("\n")
        const kept = []
        let stripping = true
        for (let i = 0; i < lines.length; ++i) {
            const t = lines[i].trim()
            const headerLike = /^content-|^mime-version:|^x-/i.test(t)
            const boundaryLike = /^--[_=A-Za-z0-9.:-]+$/.test(t)
            if (stripping && (headerLike || boundaryLike || t === "")) {
                if (t === "" && i > 0) stripping = false
                continue
            }
            stripping = false
            kept.push(lines[i])
        }
        html = kept.join("\n").trim()

        // Ensure we always hand WebEngine a full document.
        const hasHtmlTag = /<html\b/i.test(html)
        const hasBodyTag = /<body\b/i.test(html)
        const hasHtmlish = /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html)

        if (!hasHtmlish) {
            const escaped = html.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
            return "<!doctype html><html><head><meta charset='utf-8'></head><body><pre style='white-space:pre-wrap;font-family:sans-serif;'>" + escaped + "</pre></body></html>"
        }

        if (hasHtmlTag) {
            if (!/<head\b/i.test(html)) {
                html = html.replace(/<html[^>]*>/i, function(m) { return m + "<head><meta charset='utf-8'></head>" })
            } else if (!/<meta\s+charset=/i.test(html)) {
                html = html.replace(/<head[^>]*>/i, function(m) { return m + "<meta charset='utf-8'>" })
            }
            if (!hasBodyTag) {
                html = html.replace(/<\/html>\s*$/i, "<body></body></html>")
            }
            return html
        }

        if (hasBodyTag) {
            return "<!doctype html><html><head><meta charset='utf-8'></head>" + html + "</html>"
        }

        return "<!doctype html><html><head><meta charset='utf-8'></head><body>" + html + "</body></html>"
    }

    function darkenHtml(html) {
        const darkBg      = Kirigami.Theme.backgroundColor.toString()
        const surfaceBg   = Kirigami.Theme.alternateBackgroundColor.toString()
        const lightText   = Kirigami.Theme.textColor.toString()
        const borderColor = Kirigami.Theme.disabledTextColor.toString()

        const style = "<style data-dark-mode='baseline'>"
                    + "html, body { background-color:" + darkBg + " !important; color:" + lightText + " !important; }"
                    + "[color] { color:" + lightText + " !important; }"
                    + "td[style*='border'], div[style*='border'], table[style*='border'] { border-color:" + borderColor + " !important; }"
                    + "hr { border-color:" + borderColor + " !important; }"
                    + "img, svg, canvas, video, picture, iframe { filter:none !important; mix-blend-mode:normal !important; opacity:1 !important; }"
                    + "</style>";

        const script = "<script>(function(){"
                    + "var DARK_BG='" + darkBg + "',SURFACE_BG='" + surfaceBg + "',LIGHT_TEXT='" + lightText + "',BORDER_COLOR='" + borderColor + "';"
                    + "var MEDIA={IMG:1,SVG:1,CANVAS:1,VIDEO:1,PICTURE:1,IFRAME:1};"
                    + "function parseRGB(c){if(!c)return null;var m=c.match(/rgba?\\(\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)/);return m?[+m[1],+m[2],+m[3]]:null;}"
                    + "function lum(r,g,b){var a=[r/255,g/255,b/255];for(var i=0;i<3;i++){a[i]=a[i]<=0.03928?a[i]/12.92:Math.pow((a[i]+0.055)/1.055,2.4);}return 0.2126*a[0]+0.7152*a[1]+0.0722*a[2];}"
                    + "function isLight(c){var r=parseRGB(c);return r?lum(r[0],r[1],r[2])>0.35:false;}"
                    + "function isTransparent(c){if(!c)return true;return c==='transparent'||/rgba?\\(.*,\\s*0\\)$/.test(c);}"
                    // isSaturated: R/G/B spread > 20 means a real design color, not a neutral gray.
                    // Threshold at 20 (not 30) preserves subtle tints like avatar circle backgrounds
                    // (#CAD3E5, spread=27) as well as vivid accent colours (#4CD681, spread=138).
                    + "function isSaturated(rgb){return Math.max(rgb[0],rgb[1],rgb[2])-Math.min(rgb[0],rgb[1],rgb[2])>20;}"
                    // isImageShell: element contains only media — no real text at any depth.
                    // Uses \s+ which matches U+00A0 (&nbsp;), unlike String.trim().
                    + "function isImageShell(el){if(!el.querySelector||!el.querySelector('img,svg,canvas,video,picture'))return false;return(el.textContent||'').replace(/\\s+/g,'').length===0;}"
                    // isTextLink: <a> that contains no block-level elements is a genuine hyperlink
                    // (→ blue); an <a> that wraps a card/section of content is a layout wrapper
                    // (→ LIGHT_TEXT, so its children keep their own inline colors).
                    + "function isTextLink(el){return !el.querySelector('div,table,p,h1,h2,h3,h4,h5,h6,section,article,header,footer');}"
                    // effectiveBg: walk up the DOM to find the nearest non-transparent background.
                    // Elements are processed in document order, so parent backgrounds already reflect
                    // our darkening changes by the time any child is visited.
                    + "function effectiveBg(el,doc){var cur=el;while(cur){var b=doc.defaultView.getComputedStyle(cur).backgroundColor;if(!isTransparent(b))return b;cur=cur.parentElement;}return DARK_BG;}"
                    + "function processEl(el,doc){"
                    +   "if(MEDIA[el.tagName])return;"
                    +   "var cs=doc.defaultView.getComputedStyle(el);if(!cs)return;"
                    +   "var bg=cs.backgroundColor,fg=cs.color,hasBgImg=(cs.backgroundImage&&cs.backgroundImage!=='none')||el.hasAttribute('background');"
                    // Background: leave elements with CSS background-image or saturated accent colors alone.
                    // Make pure image shells with neutral light bg transparent; darken neutral light bg otherwise.
                    +   "if(!isTransparent(bg)&&!hasBgImg){"
                    +     "var rgb=parseRGB(bg);"
                    +     "if(rgb&&lum(rgb[0],rgb[1],rgb[2])>0.7&&!isSaturated(rgb)){"
                    +       "if(isImageShell(el)){el.style.setProperty('background-color','transparent','important');}"
                    +       "else{var tgt=lum(rgb[0],rgb[1],rgb[2])>0.97?DARK_BG:SURFACE_BG;el.style.setProperty('background','none','important');el.style.setProperty('background-color',tgt,'important');}"
                    +     "}"
                    +   "}"
                    // Text color: only intervene when the actual WCAG contrast ratio between
                    // the text and its effective background is below 3:1.  Text that is already
                    // readable (including intentional link blues, branded colours, etc.) is left
                    // completely untouched.  Text that is genuinely invisible on the dark surface
                    // (e.g. black on #2d2d2d) gets lifted to:
                    //   • LIGHT_TEXT — for block wrapper <a> tags and neutral-colored text
                    //   • #7ab4f5   — for genuine text hyperlinks and saturated-color inline text
                    //                 (e.g. <span style="color:#0076B6">See more</span>)
                    +   "var fgR=parseRGB(fg);if(fgR){var fgL=lum(fgR[0],fgR[1],fgR[2]);if(fgL<0.35){var eff=effectiveBg(el,doc);var effR=parseRGB(eff)||[30,30,30];var effL=lum(effR[0],effR[1],effR[2]);var lo=Math.min(fgL,effL),hi=Math.max(fgL,effL);if((hi+0.05)/(lo+0.05)<3){var target=LIGHT_TEXT;if(el.tagName==='A'){target=isTextLink(el)?'#7ab4f5':LIGHT_TEXT;}else if(Math.max(fgR[0],fgR[1],fgR[2])-Math.min(fgR[0],fgR[1],fgR[2])>150){target='#7ab4f5';}el.style.setProperty('color',target,'important');}}}"
                    +   "var bc=cs.borderColor;if(bc&&isLight(bc))el.style.setProperty('border-color',BORDER_COLOR,'important');"
                    + "}"
                    + "function run(){try{var d=document;if(!d||!d.body)return;var all=d.querySelectorAll('*');for(var i=0;i<all.length;i++)processEl(all[i],d);}catch(e){console.log('kestrel-dark-error',e);}}"
                    + "if(document.readyState==='complete'||document.readyState==='interactive')run();else document.addEventListener('DOMContentLoaded',run,{once:true});"
                    + "})();</script>";

        const inject = style + script;
        if (html.indexOf("<head>") >= 0) {
            const out = html.replace("<head>", "<head>" + inject)
            return out
        }
        if (html.indexOf("<html") >= 0) {
            const out = html.replace(/<html[^>]*>/i, function(m) { return m + "<head>" + inject + "</head>" })
            return out
        }
        const out = "<html><head>" + inject + "</head><body>" + html + "</body></html>"
        return out
    }

    function decodeMailtoComponent(s) {
        return decodeURIComponent((s || "").replace(/\+/g, "%20"))
    }

    function handleMailtoUrl(urlString) {
        const raw = (urlString || "").toString()
        if (!raw.toLowerCase().startsWith("mailto:")) return false

        const noScheme = raw.slice(7)
        const q = noScheme.indexOf("?")
        const address = decodeMailtoComponent(q >= 0 ? noScheme.slice(0, q) : noScheme).trim()
        const query = q >= 0 ? noScheme.slice(q + 1) : ""

        let subject = ""
        let body = ""
        let cc = ""
        let bcc = ""

        if (query.length) {
            const pairs = query.split("&")
            for (let i = 0; i < pairs.length; ++i) {
                const p = pairs[i]
                const eq = p.indexOf("=")
                const k = (eq >= 0 ? p.slice(0, eq) : p).toLowerCase()
                const v = decodeMailtoComponent(eq >= 0 ? p.slice(eq + 1) : "")
                if (k === "subject") subject = v
                else if (k === "body") body = v
                else if (k === "cc") cc = v
                else if (k === "bcc") bcc = v
            }
        }

        console.log("[mailto] intercepted", "to=", address, "subjectLen=", subject.length, "cc=", cc.length > 0, "bcc=", bcc.length > 0)

        if (address.length) appRoot.composeDraftTo = address
        if (subject.length) appRoot.composeDraftSubject = subject
        if (body.length) appRoot.composeDraftBody = body

        if (address.length) appRoot.openComposerTo(address, i18n("mailto"))
        else appRoot.openComposeDialog(i18n("mailto"))
        return true
    }

    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.35)

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: appRoot.sectionSpacing
        anchors.bottomMargin: appRoot.sectionSpacing
        spacing: appRoot.sectionSpacing

        RowLayout {
            Layout.fillWidth: true
            visible: !!root.messageData
            spacing: 4

            QQC2.Label {
                text: root.messageSubject
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 140
            }

            Repeater {
                model: root.activeTags
                delegate: QQC2.Button {
                    required property var modelData
                    flat: true
                    text: modelData.name + "  ✕"
                    implicitHeight: 28
                    implicitWidth: Math.min(Math.max(52, contentItem.implicitWidth + leftPadding + rightPadding + 8), 150)
                    leftPadding: 10
                    rightPadding: 10
                    onClicked: root.removeTagByName(modelData.name)
                    background: Rectangle {
                        radius: height / 2
                        color: modelData.color
                        border.color: Qt.darker(modelData.color, 1.08)
                        border.width: 1
                    }
                    contentItem: QQC2.Label {
                        text: parent.text
                        color: modelData.textColor
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        maximumLineCount: 1
                    }
                }
            }

            QQC2.Button {
                id: deleteButton
                text: root.folderName + "  ✕"
                flat: true
                implicitHeight: 28
                implicitWidth: Math.min(150, Math.max(64, contentItem.implicitWidth + leftPadding + rightPadding + 10))
                leftPadding: 10
                rightPadding: 10
                onClicked: appRoot.deleteSelectedMessages()

                TextToolTip {
                    parent: QQC2.Overlay.overlay
                    visible: deleteButton.hovered
                    toolTipText: i18n("Delete this message")
                    preferredX: deleteButton.mapToItem(QQC2.Overlay.overlay, Math.round((deleteButton.width - implicitWidth) / 2), 0).x
                    preferredY: deleteButton.mapToItem(QQC2.Overlay.overlay, Math.round((deleteButton.height - implicitHeight) / 2), 0).y - implicitHeight - 6
                }

                background: Rectangle {
                    radius: height / 2
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.1)
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.35)
                    border.width: 1
                }
                contentItem: QQC2.Label {
                    text: parent.text
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            QQC2.ToolButton {
                id: tagMenuButton
                icon.name: "tag"
                display: QQC2.AbstractButton.IconOnly
                Layout.rightMargin: -2
                onClicked: tagMenu.openIfClosed()
            }
            PopupMenu {
                id: tagMenu
                parent: tagMenuButton
                verticalOffset: 4
                QQC2.MenuItem { text: i18n("Draft"); onTriggered: root.addTag({ name: i18n("Draft"), color: "#E0E0E0", textColor: "#2B2B2B" }) }
                QQC2.MenuItem { text: i18n("Junk"); onTriggered: root.addTag({ name: i18n("Junk"), color: "#FFE2D8", textColor: "#7A2E16" }) }
                QQC2.MenuItem { text: i18n("Unwanted"); onTriggered: root.addTag({ name: i18n("Unwanted"), color: "#F9D7DE", textColor: "#7A1F34" }) }
                QQC2.MenuItem { text: i18n("Important"); onTriggered: root.addTag({ name: i18n("Important"), color: "#FFEAAE", textColor: "#6A4A00" }) }
                QQC2.MenuItem { text: i18n("Home"); onTriggered: root.addTag({ name: i18n("Home"), color: "#D9F0FF", textColor: "#114D73" }) }
                QQC2.MenuItem { text: i18n("Newsletter"); onTriggered: root.addTag({ name: i18n("Newsletter"), color: "#E8E2FF", textColor: "#473088" }) }
                QQC2.MenuItem { text: i18n("Personal"); onTriggered: root.addTag({ name: i18n("Personal"), color: "#E3FBD9", textColor: "#2D6A23" }) }
                QQC2.MenuItem { text: i18n("Promotion"); onTriggered: root.addTag({ name: i18n("Promotion"), color: "#D6E8FF", textColor: "#1D4E89" }) }
                QQC2.MenuItem { text: i18n("School"); onTriggered: root.addTag({ name: i18n("School"), color: "#DCE8FF", textColor: "#1B3A73" }) }
                QQC2.MenuItem { text: i18n("Work"); onTriggered: root.addTag({ name: i18n("Work"), color: "#FFE9C9", textColor: "#7A4B08" }) }
                QQC2.MenuSeparator {}
                QQC2.MenuItem {
                    text: i18n("New tag…")
                    onTriggered: customTagDialog.open()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 28
            // Layout.bottomMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing * 2
            visible: !!root.messageData
            spacing: Kirigami.Units.smallSpacing

            Item {
                id: avatarWrap
                Layout.preferredWidth: Kirigami.Units.iconSizes.large + 8
                Layout.preferredHeight: Kirigami.Units.iconSizes.large + 8

                property var avatarSources: appRoot.senderAvatarSources(
                                                  root.senderText,
                                                  "",
                                                  "",
                                                  root.messageData && root.messageData.accountEmail ? root.messageData.accountEmail : "")

                AvatarBadge {
                    anchors.centerIn: parent
                    size: Kirigami.Units.iconSizes.large + 8
                    displayName: root.senderName
                    fallbackText: root.senderText
                    avatarSources: avatarWrap.avatarSources
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                QQC2.Label {
                    id: senderLink
                    text: root.senderName
                    font.bold: true
                    font.pixelSize: 13
                    color: "#4ea3ff"
                    Layout.fillWidth: false
                    elide: Text.ElideRight

                    MouseArea {
                        id: senderMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: appRoot.openComposerTo(root.senderEmail, i18n("sender"))
                    }

                    ContactHoverPopup {
                        id: senderPopup
                        anchorItem: senderLink
                        targetHovered: senderMouse.containsMouse
                        titleText: root.senderName
                        subtitleText: root.senderEmail
                        avatarText: appRoot.avatarInitials(root.senderName)
                        avatarSources: avatarWrap.avatarSources
                        primaryButtonText: i18n("Send mail")
                        secondaryButtonText: i18n("Add to contacts")
                        onPrimaryTriggered: appRoot.openComposerTo(root.senderEmail, i18n("tooltip send"))
                        onSecondaryTriggered: appRoot.showInlineStatus(i18n("Add to contacts requested for %1").arg(root.senderEmail), false)

                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    QQC2.Label {
                        text: i18n("to")
                        color: Kirigami.Theme.textColor
                        opacity: 0.9
                    }

                    QQC2.Label {
                        id: recipientLink
                        text: root.recipientName
                        color: "#4ea3ff"
                        opacity: 0.95
                        font.bold: true
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: false

                        MouseArea {
                            id: recipientMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appRoot.openComposerTo(root.recipientsText, i18n("recipient"))
                        }

                        ContactHoverPopup {
                            id: recipientPopup
                            anchorItem: recipientLink
                            targetHovered: recipientMouse.containsMouse
                            titleText: root.recipientName
                            subtitleText: root.recipientsText
                            avatarText: (root.recipientName || "?").slice(0,1).toUpperCase()
                            avatarSources: root.recipientAvatarSources
                            primaryButtonText: i18n("Send mail")
                            secondaryButtonText: i18n("Add to contacts")
                            onPrimaryTriggered: appRoot.openComposerTo(root.recipientsText, i18n("recipient"))
                            onSecondaryTriggered: appRoot.showInlineStatus(i18n("Add to contacts requested for %1").arg(root.recipientsText), false)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 2

                QQC2.Label {
                    text: root.receivedAtText
                    opacity: 0.82
                }

                RowLayout {
                    spacing: 6

                    MailActionButton {
                        iconName: "mail-reply-sender"
                        text: i18n("Reply")
                        menuItems: [
                            { text: i18n("Reply"), icon: "mail-reply-sender" },
                            { text: i18n("Reply all"), icon: "mail-reply-all" },
                            { text: i18n("Forward"), icon: "mail-forward" }
                        ]
                        onTriggered: function(actionText) {
                            if (actionText === i18n("Reply") || actionText.length === 0) {
                                appRoot.composeDraftSubject = i18n("Re: %1").arg(root.messageSubject)
                                appRoot.openComposerTo(root.senderEmail, i18n("reply"))
                            } else if (actionText === i18n("Reply all")) {
                                appRoot.composeDraftSubject = i18n("Re: %1").arg(root.messageSubject)
                                appRoot.openComposerTo(root.recipientsText, i18n("reply all"))
                            } else if (actionText === i18n("Forward")) {
                                appRoot.composeDraftTo = ""
                                appRoot.composeDraftSubject = i18n("Fwd: %1").arg(root.messageSubject)
                                appRoot.composeDraftBody = i18n("\n\n--- Forwarded message ---\nFrom: %1\nDate: %2\n\n%3")
                                                       .arg(root.senderText)
                                                       .arg(root.receivedAtText)
                                                       .arg((root.messageData && root.messageData.snippet) ? root.messageData.snippet : "")
                                appRoot.openComposeDialog(i18n("forward"))
                            }
                        }
                    }

                    QQC2.Button {
                        text: root.forceDarkHtml ? i18n("Dark") : i18n("Light")
                        icon.name: root.forceDarkHtml ? "weather-clear-night" : "weather-clear"
                        onClicked: {
                            if (appRoot) appRoot.contentPaneDarkModeEnabled = !appRoot.contentPaneDarkModeEnabled
                        }
                    }
                }
            }
        }

        // Info bars — outer layout is visible when any bar has something to show.
        ColumnLayout {
            id: infoBars
            visible: !!root.messageData && (
                (root.hasExternalImages && !root.imagesAllowed && !root.isTrustedSender) ||
                (root.hasTrackingPixel  && !root.trackingAllowed && root.imagesAllowed) ||
                root.listUnsubscribeUrl.length > 0
            )

            Layout.fillWidth: true
            Layout.bottomMargin: Kirigami.Units.largeSpacing * 2
            spacing: Kirigami.Units.largeSpacing

            // Unsubscribe Info Bar
            RowLayout {
                id: unsubInfo
                Layout.alignment: Qt.AlignLeft
                spacing: Kirigami.Units.smallSpacing
                visible: !!root.messageData && root.listUnsubscribeUrl.length > 0

                Kirigami.Icon {
                    source: "help-contextual";
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignLeft
                }

                QQC2.Label {
                    text: i18n("Unsubscribe")
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: "#4ea3ff"
                    Layout.fillWidth: false

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: { Qt.openUrlExternally(root.listUnsubscribeUrl) }
                    }
                }

                QQC2.Label {
                    text: i18n("from receiving these messages")
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: Kirigami.Theme.textColor
                    Layout.fillWidth: false
                }
            }

            // Tracker Info Bar
            RowLayout {
                id: trackerInfo
                Layout.alignment: Qt.AlignLeft
                spacing: Kirigami.Units.smallSpacing
                visible: !!root.messageData && root.hasTrackingPixel && !root.trackingAllowed && root.imagesAllowed

                Kirigami.Icon {
                    source: "crosshairs";
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignLeft
                }

                QQC2.Label {
                    text: i18n("Allow email tracking.")
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: "#4ea3ff"
                    Layout.fillWidth: false

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: { root.trackingAllowed = true }
                    }
                }

                QQC2.Label {
                    text: (root.trackerVendor || "Email") + i18n(" tracking was blocked to preserve privacy.")
                    // text: i18n("email tracking was blocked to preserve privacy.")
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: Kirigami.Theme.textColor
                    Layout.fillWidth: false
                }
            }

            // Images Info Bar — hidden when a tracking pixel is present (tracker bar takes priority)
            RowLayout {
                id: downloadInfo
                Layout.alignment: Qt.AlignLeft
                spacing: Kirigami.Units.smallSpacing
                visible: !!root.messageData && root.hasExternalImages && !root.imagesAllowed
                         && !root.isTrustedSender

                Kirigami.Icon {
                    source: "messagebox_warning";
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignLeft
                }

                QQC2.Label {
                    text: i18n("Download Pictures")
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: "#4ea3ff"
                    Layout.fillWidth: false

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: { root.imagesAllowed = true }
                    }
                }

                QQC2.Label {
                    text: i18n("or")
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: Kirigami.Theme.textColor
                    Layout.fillWidth: false
                }

                QQC2.Label {
                    text: i18n("always download pictures from this sender.")
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: "#4ea3ff"
                    Layout.fillWidth: false

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: {
                            if (root.senderDomain && appRoot && appRoot.dataStoreObj)
                                appRoot.dataStoreObj.setTrustedSenderDomain(root.senderDomain)
                            root.imagesAllowed = true
                        }
                    }
                }

                QQC2.Label {
                    text: i18n("To preserve privacy, external content was not downloaded.")
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    color: Kirigami.Theme.textColor
                    Layout.fillWidth: false
                }
            }
        }

        Flow {
            id: attachmentFlow
            Layout.fillWidth: true
            visible: !!root.messageData && root.attachmentItems.length > 0
            spacing: Kirigami.Units.smallSpacing
            Layout.bottomMargin: visible ? Kirigami.Units.largeSpacing : 0

            Repeater {
                model: root.attachmentItems

                delegate: Rectangle {
                    id: attachmentCard
                    required property var modelData
                    property bool pressed: false
                    radius: 8
                    color: pressed ? root.systemPalette.highlight : Qt.lighter(Kirigami.Theme.backgroundColor, 1.08)
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                    border.width: 1
                    height: 34
                    width: Math.min(320, Math.max(180, nameLabel.implicitWidth + 60))

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Kirigami.Icon {
                            source: "mail-attachment"
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }

                        QQC2.Label {
                            id: nameLabel
                            text: attachmentCard.modelData.name
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    HoverHandler {
                        id: attachmentHover
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: function(mouse) {
                            attachmentCard.pressed = (mouse.button === Qt.LeftButton)
                        }
                        onReleased: function() {
                            attachmentCard.pressed = false
                        }
                        onCanceled: attachmentCard.pressed = false
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.LeftButton)
                                appRoot.imapServiceObj.openAttachmentUrl(attachmentCard.modelData.url)
                            else if (mouse.button === Qt.RightButton)
                                attachmentMenu.popup()
                        }
                    }

                    QQC2.Menu {
                        id: attachmentMenu
                        QQC2.MenuItem {
                            text: i18n("Open")
                            onTriggered: appRoot.imapServiceObj.openAttachmentUrl(attachmentCard.modelData.url)
                        }
                        QQC2.MenuItem {
                            text: i18n("Save as…")
                            onTriggered: appRoot.imapServiceObj.saveAttachmentUrl(attachmentCard.modelData.url, attachmentCard.modelData.name)
                        }
                    }

                    ContactHoverPopup {
                        id: attachmentPopup
                        anchorItem: attachmentCard
                        targetHovered: attachmentHover.hovered
                        titleText: attachmentCard.modelData.name
                        subtitleText: attachmentCard.modelData.url
                        avatarText: ""
                        avatarSources: attachmentCard.modelData.canPreview ? [attachmentCard.modelData.url] : []
                        primaryButtonText: i18n("Open")
                        secondaryButtonText: i18n("Save")
                        onPrimaryTriggered: appRoot.imapServiceObj.openAttachmentUrl(attachmentCard.modelData.url)
                        onSecondaryTriggered: appRoot.imapServiceObj.saveAttachmentUrl(attachmentCard.modelData.url, attachmentCard.modelData.name)
                    }
                }
            }
        }

        Rectangle {
            id: htmlContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !!root.messageData
            color: "transparent"

            property real flickVelocityY: 0
            property string lastLoadedHtmlKey: ""
            property real bodyOpacity: 1.0
            property string pendingHtml: ""
            property string pendingLoadReason: ""

            Behavior on bodyOpacity {
                NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
            }

            function loadHtmlIfChanged(reason) {
                if (!root.renderedHtml.length)
                    return

                const key = (root.imagesAllowed ? "1" : "0") + "|" + root.renderedHtml
                if (key === lastLoadedHtmlKey)
                    return

                lastLoadedHtmlKey = key
                pendingHtml = root.renderedHtml
                pendingLoadReason = reason
                bodyOpacity = 0.0
                fadeOutLoadTimer.restart()
            }

            function scrollHtmlBy(deltaY) {
                if (!root.hasUsableBodyHtml) return
                const amount = Number(deltaY)
                if (!isFinite(amount) || Math.abs(amount) < 0.01) return
                htmlView.runJavaScript("window.scrollBy(0," + Math.round(amount) + ");")
            }

            Timer {
                id: fadeOutLoadTimer
                interval: 250
                repeat: false
                onTriggered: {
                    if (!htmlContainer.pendingHtml.length)
                        return
                    htmlView.loadHtml(htmlContainer.pendingHtml, "http://kestrel.local/")
                }
            }

            WebEngineView {
                id: htmlView
                anchors.fill: parent
                visible: root.hasUsableBodyHtml
                opacity: htmlContainer.bodyOpacity
                settings.localContentCanAccessRemoteUrls: true
                settings.autoLoadImages: root.imagesAllowed
                settings.errorPageEnabled: true

                onNavigationRequested: function(request) {
                    const url = request.url ? request.url.toString() : ""
                    if (!url.length) return

                    if (url.toLowerCase().startsWith("mailto:")) {
                        request.action = WebEngineNavigationRequest.IgnoreRequest
                        if (!root.handleMailtoUrl(url)) {
                            console.warn("[mailto] failed to handle; opening externally")
                            Qt.openUrlExternally(url)
                        }
                        return
                    }

                    if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation
                            && (url.startsWith("http://") || url.startsWith("https://"))) {
                        request.action = WebEngineNavigationRequest.IgnoreRequest
                        Qt.openUrlExternally(url)
                    }
                }

                // Links with target="_blank" fire onNewWindowRequested instead of
                // onNavigationRequested — handle them the same way.
                onNewWindowRequested: function(request) {
                    const url = request.requestedUrl ? request.requestedUrl.toString() : ""
                    if (url.startsWith("http://") || url.startsWith("https://"))
                        Qt.openUrlExternally(url)
                    else if (url.toLowerCase().startsWith("mailto:"))
                        Qt.openUrlExternally(url)
                }

                onLoadingChanged: function(req) {
                    const st = req.status
                    if (st === WebEngineLoadingInfo.LoadSucceededStatus || st === WebEngineLoadingInfo.LoadFailedStatus) {
                        htmlContainer.pendingHtml = ""
                        htmlContainer.bodyOpacity = 1.0
                    }

                }

                Component.onCompleted: htmlContainer.loadHtmlIfChanged("component")

                Connections {
                    target: root
                    function onRenderedHtmlChanged() {
                        htmlContainer.loadHtmlIfChanged("renderedHtml")
                    }
                    function onImagesAllowedChanged() {
                        htmlContainer.loadHtmlIfChanged("imagesAllowed")
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.messageData

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon { source: "mail-message"; width: 42; height: 42; Layout.alignment: Qt.AlignHCenter }
                QQC2.Label { text: i18n("Select a message"); font.bold: true; Layout.alignment: Qt.AlignHCenter }
                QQC2.Label { text: i18n("Choose an email from the list to view its content."); opacity: 0.75; Layout.alignment: Qt.AlignHCenter }
            }
        }
    }

    Column {
        id: inlineMessageStack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: appRoot.sectionSpacing
        spacing: 6
        z: 20

        Repeater {
            model: appRoot.inlineStatusQueue || []
            delegate: Kirigami.InlineMessage {
                id: inlineMsg
                required property var modelData
                property bool closing: false
                width: inlineMessageStack.width
                visible: true
                text: modelData.text || ""
                type: modelData.isError ? Kirigami.MessageType.Error : Kirigami.MessageType.Positive
                showCloseButton: true
                opacity: closing ? 0 : 1
                onLinkActivated: Qt.openUrlExternally(link)
                onVisibleChanged: {
                    if (!visible) {
                        appRoot.dismissInlineStatus(modelData.id)
                    }
                }
                Timer {
                    interval: 5000
                    running: true
                    repeat: false
                    onTriggered: inlineMsg.closing = true
                }
                Timer {
                    interval: 220
                    running: inlineMsg.closing
                    repeat: false
                    onTriggered: appRoot.dismissInlineStatus(inlineMsg.modelData.id)
                }
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }
        }
    }

    QQC2.Dialog {
        id: customTagDialog
        title: i18n("Create tag")
        modal: true
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        width: 320

        property string pendingTagName: ""

        contentItem: ColumnLayout {
            anchors.fill: parent
            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("Tag name")
                text: customTagDialog.pendingTagName
                onTextChanged: customTagDialog.pendingTagName = text
            }
        }

        onAccepted: {
            const name = pendingTagName.trim()
            if (!name.length) return
            root.addTag({ name: name, color: "#E6EEF8", textColor: "#1E3C5A" })
            pendingTagName = ""
        }
        onRejected: pendingTagName = ""
    }
}
