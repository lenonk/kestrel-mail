import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import ".."

Rectangle {
    id: root

    // Tracking pixel detection: scans for 1×1 external <img> tags to confirm tracking is
    // present, then determines the vendor using headers before falling back to the pixel URL.
    // Priority: espVendor (C++ pre-computed from X-Mailgun-Sid / Received chain / etc.) →
    //           X-Mailer first word → pixel URL SLD → Return-Path domain → DKIM domain.
    readonly property var _trackerInfo: {
        const html = (root.messageBodyHtml || "").toString();
        const espVendor = root.messageData ? (root.messageData.espVendor || "").toString() : "";
        const xMailer = root.messageData ? (root.messageData.xMailer || "").toString() : "";
        const returnPath = root.messageData ? (root.messageData.returnPath || "").toString() : "";
        const authResults = root.messageData ? (root.messageData.authResults || "").toString() : "";

        // Scan for a 1×1 external tracking pixel — this is the trigger for the whole bar.
        // First-party pixels (sender's own domain) are skipped UNLESS a definitive ESP header
        // (X-Mailgun-Sid, X-SG-EID, etc.) is present: many ESPs use custom CNAME tracking
        // domains that look first-party but route through the ESP's infrastructure.
        const senderDom = root.senderDomain || "";
        const tagRe = /<img\b[^>]*>/gi;
        let pixelSrc = "";
        let m;
        while ((m = tagRe.exec(html)) !== null) {
            const tag = m[0];
            if (!/\bsrc\s*=\s*["']https?:/i.test(tag))
                continue;
            if (!/\bwidth\s*=\s*["']\s*1\s*["']/i.test(tag))
                continue;
            if (!/\bheight\s*=\s*["']\s*1\s*["']/i.test(tag))
                continue;
            const srcM = tag.match(/\bsrc\s*=\s*["']([^"']+)/i);
            if (!srcM)
                continue;
            const candidateSrc = srcM[1];
            if (isFirstPartyUrl(candidateSrc, senderDom)) {
                continue;
            }
            pixelSrc = candidateSrc;
            break;
        }
        if (!pixelSrc)
            return ({
                    found: false,
                    vendor: ""
                });

        // Determine vendor from best available signal (richest first).
        const vendor = espVendor || vendorFromXMailer(xMailer) || vendorFromUrl(pixelSrc) || vendorFromReturnPath(returnPath) || vendorFromAuthResults(authResults);
        return ({
                found: true,
                vendor
            });
    }
    readonly property var activeTags: {
        const key = appRoot.selectedMessageKey || "";
        if (!key.length)
            return [];
        return tagMap[key] ? tagMap[key] : [];
    }
    required property var appRoot
    property var attachmentDownloading: ({})
    property var attachmentItems: []
    property var attachmentLocalPaths: ({})
    property var attachmentProgress: ({})
    property var cardDarkModes: ({})      // index → bool, overrides global dark mode per card

    readonly property string folderName: i18n("Inbox")
    property bool forceDarkHtml: !!(appRoot ? appRoot.contentPaneDarkModeEnabled : true)
    readonly property bool hasExternalImages: {
        const html = (root.messageBodyHtml || "").toString();
        return /src\s*=\s*["']https?:\/\//i.test(html);
    }
    readonly property bool hasTrackingPixel: root._trackerInfo.found
    readonly property bool hasUsableBodyHtml: {
        const html = (messageBodyHtml || "").toString().trim();
        if (!html.length)
            return false;
        const lower = html.toLowerCase();
        if (lower.indexOf("ok success [throttled]") >= 0)
            return false;
        if (lower.indexOf("authenticationfailed") >= 0)
            return false;
        if (/\bsrc\s*=\s*["']\s*cid:/i.test(html))
            return false;
        return /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html);
    }
    property bool imagesAllowed: false
    readonly property bool immersiveBodyMode: !!(appRoot && appRoot.contentPaneHoverExpandActive)
    readonly property bool isThreadView: threadMessages.length > 1
    // Reads directly from messageData.sender so it updates atomically with the message,
    // avoiding the senderDomain → senderEmail → senderText binding chain lag.
    readonly property bool isTrustedSender: {
        const senderVal = (root.messageData && root.messageData.sender) ? root.messageData.sender.toString() : "";
        if (!senderVal)
            return false;

        let email = senderVal;

        const angleMatch = senderVal.match(/<([^>]+@[^>]+)>/);
        if (angleMatch)
            email = angleMatch[1];

        const at = email.lastIndexOf('@');
        const domain = at >= 0 ? email.slice(at + 1).trim().toLowerCase() : "";

        return !!(domain && appRoot && appRoot.dataStoreObj && appRoot.dataStoreObj.isSenderTrusted(domain));
    }
    property string lastAttachmentMessageKey: ""
    readonly property string listUnsubscribeUrl: {
        return (root.messageData && root.messageData.listUnsubscribe) ? root.messageData.listUnsubscribe.toString().trim() : "";
    }
    readonly property string messageBodyHtml: (messageData && messageData.bodyHtml) ? messageData.bodyHtml : ""
    readonly property var messageData: appRoot.selectedMessageData
    readonly property string messageSubject: (messageData && messageData.subject) ? messageData.subject : i18n("(No subject)")
    readonly property string receivedAtText: appRoot.formatContentDate(messageData ? messageData.receivedAt : "")
    readonly property var recipientAvatarSources: appRoot.senderAvatarSources(recipientsText, "", "", messageData && messageData.accountEmail ? messageData.accountEmail : "")
    readonly property string recipientName: appRoot.displayRecipientNames(recipientsText, messageData && messageData.accountEmail ? messageData.accountEmail : "")
    readonly property string recipientsText: (messageData && messageData.recipient && messageData.recipient.toString().trim().length > 0) ? messageData.recipient : ((messageData && messageData.accountEmail) ? messageData.accountEmail : i18n("No recipient"))
    readonly property string renderMessageKey: {
        if (!messageData)
            return "";
        return normalizedEdgeKey(messageData.accountEmail, messageData.folder, messageData.uid);
    }
    readonly property string renderedHtml: {
        if (!root.hasUsableBodyHtml)
            return "";

        // Sync theme colors to C++ processor (reading these creates reactive deps).
        htmlProcessor.darkBg      = Qt.darker(Kirigami.Theme.backgroundColor, 1.35).toString();
        htmlProcessor.surfaceBg   = Kirigami.Theme.alternateBackgroundColor.toString();
        htmlProcessor.lightText   = Kirigami.Theme.textColor.toString();
        htmlProcessor.borderColor = Kirigami.Theme.disabledTextColor.toString();

        let html = htmlProcessor.sanitize(root.htmlForMessage());

        if (!root.trackingAllowed)
            html = htmlProcessor.neutralizeTrackingPixels(html, root.senderDomain || "");

        if (!root.imagesAllowed)
            html = htmlProcessor.neutralizeExternalImages(html);

        return htmlProcessor.prepare(html, root.forceDarkHtml);
    }
    property string selectedAttachmentKey: ""
    readonly property string selectedMessageEdgeKey: {
        const k = (root.appRoot && root.appRoot.selectedMessageKey) ? root.appRoot.selectedMessageKey.toString() : "";
        if (!k.length)
            return "";
        const raw = k.startsWith("msg:") ? k.slice(4) : k;
        const p = raw.split("|");
        if (p.length < 3)
            return "";
        return normalizedEdgeKey(p[0], p[1], p[2]);
    }

    // Read domain directly from messageData.sender to avoid transient binding lag
    // through senderText/senderEmail during selection updates.
    readonly property string senderDomain: {
        const senderVal = (root.messageData && root.messageData.sender) ? root.messageData.sender.toString() : "";
        if (!senderVal)
            return "";

        let email = senderVal;
        const angleMatch = senderVal.match(/<([^>]+@[^>]+)>/);
        if (angleMatch)
            email = angleMatch[1];

        const at = email.lastIndexOf('@');
        return at >= 0 ? email.slice(at + 1).trim().toLowerCase() : "";
    }
    readonly property string senderEmail: appRoot.senderEmail(senderText)
    readonly property string senderName: appRoot.displaySenderName(senderText, messageData && messageData.accountEmail ? messageData.accountEmail : "")
    readonly property string senderText: messageData && messageData.sender ? messageData.sender : i18n("Unknown sender")
    property bool showHeaderMeta: true
    required property var systemPalette
    property var tagMap: ({})
    readonly property int threadCount: (messageData && messageData.threadCount) ? messageData.threadCount : 0
    // null = default state (last card auto-expanded); otherwise an object used as a Set
    property var threadExpandedSet: null
    readonly property int threadHiddenCount: Math.max(0, threadMessages.length - visibleThreadMessages.length)

    // ── Thread / conversation view ───────────────────────────────────────────
    readonly property string threadId: (messageData && messageData.threadId) ? messageData.threadId.toString() : ""
    property bool threadLoadingOlder: false
    readonly property var threadMessages: {
        if (!threadId.length || threadCount < 2 || !appRoot || !appRoot.dataStoreObj)
            return [];
        // Reading .inbox creates a dependency on inboxChanged, so the thread
        // re-queries whenever any body is hydrated or messages are updated.
        void appRoot.dataStoreObj.inbox;
        return appRoot.dataStoreObj.messagesForThread(messageData.accountEmail, threadId);
    }
    readonly property int threadOffTopCount: threadHiddenCount + threadScrolledOffTopCount
    property int threadScrollEpoch: 0
    readonly property int threadScrolledOffTopCount: {
        void threadScrollEpoch;
        return _threadScrolledOffTopCount();
    }
    property bool threadShowAll: false
    property int threadVisibleCount: 5
    readonly property string trackerVendor: root._trackerInfo.vendor
    property bool trackingAllowed: false
    readonly property var visibleThreadMessages: {
        const msgs = threadMessages;
        if (threadShowAll || msgs.length <= threadVisibleCount)
            return msgs;
        return msgs.slice(msgs.length - threadVisibleCount);
    }

    // ── End thread helpers ───────────────────────────────────────────────────

    function _fileNameFromUrl(url) {
        const raw = (url || "").toString();
        if (!raw.length)
            return i18n("Attachment");
        const noQuery = raw.split("?")[0];
        const parts = noQuery.split("/");
        const last = parts.length ? parts[parts.length - 1] : "";
        return last.length ? decodeURIComponent(last) : i18n("Attachment");
    }
    function _threadAvatarSources(msg) {
        if (!msg || !msg.sender)
            return [];
        return appRoot.senderAvatarSources(msg.sender, msg.avatarDomain || "", msg.avatarUrl || "", msg.accountEmail || "");
    }
    function _threadBodyKey(msg) {
        if (!msg)
            return "";
        return (msg.accountEmail || "") + "|" + (msg.folder || "") + "|" + (msg.uid || "") + "|thread";
    }
    function _threadBodyTextForCard(index) {
        void cardDarkModes;  // reactive dependency: re-evaluate when dark mode changes
        const msgs = visibleThreadMessages;
        if (index < 0 || index >= msgs.length)
            return "";
        const msg = msgs[index];
        if (!msg.bodyHtml)
            return "";
        return renderedHtmlForThread(msg.bodyHtml.toString(), _threadCardDarkMode(index));
    }
    function _threadCardDarkMode(index) {
        return cardDarkModes[index] !== undefined ? cardDarkModes[index] : forceDarkHtml;
    }
    function _threadCardHeight(index) {
        return _threadIsExpanded(index) ? threadExpandedBodyHeight(index) : 72;
    }
    function _threadClampScrollToLastCardTop() {
        const count = visibleThreadMessages.length;
        if (count <= 0)
            return;
        // When the last card is expanded the user needs to scroll into its body,
        // so don't clamp the scroll position.
        if (_threadIsExpanded(count - 1))
            return;
        const lastItem = threadCardsRepeater.itemAt(count - 1);
        if (!lastItem)
            // delegates not yet created = skip to avoid resetting scroll position
            return;
        const maxY = Math.max(0, lastItem.mapToItem(threadScrollContent, 0, 0).y);
        if (threadFlickable.contentY > maxY)
            threadFlickable.contentY = maxY;
    }
    function _threadDate(msg) {
        if (!msg)
            return "";
        return appRoot.formatContentDate(msg.receivedAt || "");
    }
    function _threadHasBody(msg) {
        if (!msg || !msg.bodyHtml)
            return false;
        const h = msg.bodyHtml.toString();
        return h.length > 0 && /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(h);
    }
    function _threadHydrateIfNeeded(msg) {
        if (!msg || !appRoot || !appRoot.imapServiceObj)
            return;
        if (_threadHasBody(msg))
            return;
        appRoot.imapServiceObj.hydrateMessageBody(msg.accountEmail, msg.folder, msg.uid);
    }
    function _threadIsExpanded(index) {
        if (threadExpandedSet === null)
            return index === visibleThreadMessages.length - 1;
        return !!threadExpandedSet[index];
    }
    function _threadLoadOlder(step) {
        const inc = Math.max(1, step || 5);
        if (threadLoadingOlder || threadHiddenCount <= 0)
            return;
        const oldHeight = threadScrollContent.implicitHeight;
        const oldY = threadFlickable.contentY;

        threadLoadingOlder = true;
        threadVisibleCount = Math.min(threadMessages.length, threadVisibleCount + inc);

        Qt.callLater(function () {
            const delta = Math.max(0, threadScrollContent.implicitHeight - oldHeight);
            threadFlickable.contentY = oldY + delta;
            threadLoadingOlder = false;
            _threadClampScrollToLastCardTop();
        });
    }
    function _threadMarkAllRead() {
        const msgs = threadMessages;
        for (let i = 0; i < msgs.length; i++) {
            const m = msgs[i];
            if (m.unread && appRoot.imapServiceObj)
                appRoot.imapServiceObj.markMessageRead(m.accountEmail, m.folder, m.uid);
        }
    }
    function _threadMaxContentY() {
        const count = visibleThreadMessages.length;
        if (count <= 0)
            return 0;
        // When the last card is expanded allow scrolling to the natural content bottom.
        if (_threadIsExpanded(count - 1))
            return Math.max(0, threadScrollContent.implicitHeight - threadFlickable.height);
        const lastItem = threadCardsRepeater.itemAt(count - 1);
        if (!lastItem)
            return Math.max(0, threadScrollContent.implicitHeight - threadFlickable.height);
        const yPos = lastItem.mapToItem(threadScrollContent, 0, 0).y;
        return Math.max(0, yPos);
    }
    function _threadMessageKey(msg) {
        if (!msg)
            return "";
        return normalizedEdgeKey(msg.accountEmail, msg.folder, msg.uid);
    }
    function _threadOnCardExpanded(index) {
        const msgs = visibleThreadMessages;
        if (index < 0 || index >= msgs.length)
            return;
        _threadHydrateIfNeeded(msgs[index]);
    }
    function _threadOnSelectedChanged() {
        threadShowAll = false;
        threadVisibleCount = 5;
        threadLoadingOlder = false;
        threadScrollEpoch = 0;
        threadExpandedSet = null;
        cardDarkModes = ({});
    }
    function _threadOpenExternal(msg) {
        if (!msg)
            return;
        Qt.openUrlExternally("mailto:" + (msg.sender || ""));
    }
    function _threadParticipantsSummary() {
        const msgs = threadMessages;
        const me = messageData ? (messageData.accountEmail || "").toLowerCase() : "";
        const names = [];
        const seen = new Set();
        for (let i = 0; i < msgs.length; i++) {
            const name = _threadSenderName(msgs[i]);
            if (!name.length)
                continue;
            const key = name.toLowerCase();
            if (seen.has(key))
                continue;
            seen.add(key);
            const senderEmail = appRoot.senderEmail(msgs[i].sender || "");
            names.push(senderEmail.toLowerCase() === me ? i18n("me") : name);
        }
        return names.join(", ");
    }
    function _threadRecipientName(msg) {
        if (!msg || !msg.recipient)
            return "";
        return appRoot.displayRecipientNames(msg.recipient, msg.accountEmail || "");
    }
    function _threadRenderedHtml(msg, darkMode) {
        if (!msg || !msg.bodyHtml)
            return "";
        return renderedHtmlForThread(msg.bodyHtml.toString(), darkMode);
    }
    function _threadReply(msg) {
        if (!msg)
            return;
        appRoot.selectedMessageKey = "msg:" + _threadMessageKey(msg);
        // small delay to let selectedMessageData settle
        appRoot.openComposerTo(msg.replyTo || msg.sender, "reply");
    }
    function _threadScrollToCard(index) {
        const item = threadCardsRepeater.itemAt(index);
        if (!item)
            return;
        const yPos = item.mapToItem(threadScrollContent, 0, 0).y;
        threadFlickable.contentY = Math.max(0, yPos - 8);
    }
    function _threadScrolledOffTopCount() {
        const yTop = threadFlickable.contentY;
        let count = 0;
        const total = visibleThreadMessages.length;
        for (let i = 0; i < total; i++) {
            const item = threadCardsRepeater.itemAt(i);
            if (!item)
                continue;
            const y = item.mapToItem(threadScrollContent, 0, 0).y;
            // Count as off-top even if only partially clipped.
            if (y < yTop)
                count++;
            else
                break;
        }
        return count;
    }
    function _threadSenderName(msg) {
        if (!msg || !msg.sender)
            return "";
        return appRoot.displaySenderName(msg.sender, msg.accountEmail || "");
    }
    function _threadSubject() {
        return messageSubject;
    }
    function _threadToggleCardDark(index) {
        const next = Object.assign({}, cardDarkModes);
        next[index] = !_threadCardDarkMode(index);
        cardDarkModes = next;
    }
    function _threadToggleExpand(index) {
        // Materialize from default state on first user interaction
        if (threadExpandedSet === null) {
            const last = visibleThreadMessages.length - 1;
            const initial = {};
            initial[last] = true;
            threadExpandedSet = initial;
        }
        const next = Object.assign({}, threadExpandedSet);
        if (next[index])
            delete next[index];
        else
            next[index] = true;
        threadExpandedSet = next;
        Qt.callLater(function () {
            threadScrollEpoch++;
        });
    }
    function _threadUnreadCount() {
        let n = 0;
        for (let i = 0; i < threadMessages.length; i++)
            if (threadMessages[i].unread)
                n++;
        return n;
    }
    function addTag(tagObj) {
        const exists = activeTags.some(function (t) {
            return t.name === tagObj.name;
        });
        if (exists)
            return;
        const tags = activeTags.slice();
        tags.push(tagObj);
        setCurrentTags(tags);
    }
    function bodyDataImageHashes(baseHtml) {
        const html = (baseHtml || "").toString();
        const out = [];
        if (!html.length || !appRoot || !appRoot.imapServiceObj || !appRoot.imapServiceObj.dataUriSha256)
            return out;

        const imgTagRe = /<img\b[^>]*>/gi;
        let m;
        while ((m = imgTagRe.exec(html)) !== null) {
            const tag = (m[0] || "").toString();
            const srcM = tag.match(/\bsrc\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i);
            if (!srcM)
                continue;
            const src = (srcM[1] || srcM[2] || srcM[3] || "").toString().trim();
            if (!/^data:image\//i.test(src))
                continue;
            const h = appRoot.imapServiceObj.dataUriSha256(src);
            if (h && h.length)
                out.push(h);
        }
        return out;
    }
    function decodeMailtoComponent(s) {
        return decodeURIComponent((s || "").replace(/\+/g, "%20"));
    }
    function escapeHtml(text) {
        return (text || "").toString().replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }
    function extractAttachmentItemsFromHtml(html) {
        const out = [];
        const src = (html || "").toString();
        if (!src.length)
            return out;

        const seen = {};

        function maybeAdd(url, labelHint) {
            const href = (url || "").trim();
            if (!/^https?:\/\//i.test(href))
                return;
            if (seen[href])
                return;
            const lower = href.toLowerCase();
            const hasFileExt = /\.(pdf|png|jpe?g|gif|webp|heic|docx?|xlsx?|pptx?|zip|rar|7z|txt|csv)($|\?)/i.test(lower);
            const dotloopDoc = /dotloop\.com\/.+\/document\?/i.test(lower) || /[?&]documentid=/i.test(lower);
            const genericAttachment = /download|attachment|file=/i.test(lower);

            if (!(hasFileExt || dotloopDoc || genericAttachment))
                return;
            seen[href] = true;

            let label = (labelHint || "").replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim();
            if (!label.length || /^https?:\/\//i.test(label)) {
                if (dotloopDoc)
                    label = i18n("Document");
                else
                    label = root._fileNameFromUrl(href);
            }

            out.push({
                name: label,
                url: href,
                canPreview: /\.(png|jpe?g|gif|webp)($|\?)/i.test(lower)
            });
        }

        // 1) Structured anchor links
        const anchorRe = /<a\b[^>]*href\s*=\s*["']([^"']+)["'][^>]*>([\s\S]*?)<\/a>/gi;
        let m;
        while ((m = anchorRe.exec(src)) !== null)
            maybeAdd(m[1], m[2]);

        // 2) Generic href-bearing attributes
        const hrefRe = /\bhref\s*=\s*["']([^"']+)["']/gi;
        while ((m = hrefRe.exec(src)) !== null)
            maybeAdd(m[1], "");

        // 3) URL token scan for templates where links are flattened/mangled inlined HTML
        const urlRe = /https?:\/\/[^\s"'<>]+/gi;
        while ((m = urlRe.exec(src)) !== null)
            maybeAdd(m[0], "");

        return out;
    }

    function fileUrlFromLocalPath(localPath) {
        let p = (localPath || "").toString();
        if (!p.length)
            return "";
        p = p.replace(/\\/g, "/");
        // Keep '@' and path semantics intact; only encode characters that require escaping.
        return "file://" + encodeURI(p);
    }
    function formatAttachmentSize(bytes) {
        const n = Number(bytes) || 0;
        if (n <= 0)
            return "";
        if (n < 1024)
            return n + " B";
        if (n < 1024 * 1024)
            return (n / 1024).toFixed(1).replace(/\.0$/, "") + " kB";
        return (n / (1024 * 1024)).toFixed(1).replace(/\.0$/, "") + " MB";
    }
    function handleMailtoUrl(urlString) {
        const raw = (urlString || "").toString();
        if (!raw.toLowerCase().startsWith("mailto:"))
            return false;

        const noScheme = raw.slice(7);
        const q = noScheme.indexOf("?");
        const address = decodeMailtoComponent(q >= 0 ? noScheme.slice(0, q) : noScheme).trim();
        const query = q >= 0 ? noScheme.slice(q + 1) : "";

        let subject = "";
        let body = "";
        let cc = "";
        let bcc = "";

        if (query.length) {
            const pairs = query.split("&");
            for (let i = 0; i < pairs.length; ++i) {
                const p = pairs[i];
                const eq = p.indexOf("=");
                const k = (eq >= 0 ? p.slice(0, eq) : p).toLowerCase();
                const v = decodeMailtoComponent(eq >= 0 ? p.slice(eq + 1) : "");
                if (k === "subject")
                    subject = v;
                else if (k === "body")
                    body = v;
                else if (k === "cc")
                    cc = v;
                else if (k === "bcc")
                    bcc = v;
            }
        }

        if (address.length)
            appRoot.composeDraftTo = address;
        if (subject.length)
            appRoot.composeDraftSubject = subject;
        if (body.length)
            appRoot.composeDraftBody = body;

        if (address.length)
            appRoot.openComposerTo(address, i18n("mailto"));
        else
            appRoot.openComposeDialog(i18n("mailto"));
        return true;
    }
    function htmlForMessage() {
        const base = (messageBodyHtml && messageBodyHtml.trim().length > 0) ? messageBodyHtml : "";
        const inlineImages = inlineImageAttachmentsHtml(base);
        if (!inlineImages.length)
            return base;

        if (/<\/body\s*>/i.test(base))
            return base.replace(/<\/body\s*>/i, inlineImages + "</body>");
        return base + inlineImages;
    }
    function iconForAttachment(mimeType, name) {
        const mt = (mimeType || "").toLowerCase();
        const nm = (name || "").toLowerCase();
        if (mt.startsWith("image/"))
            return "image-x-generic";
        if (mt === "application/pdf")
            return "application-pdf";
        if (mt.includes("word") || nm.endsWith(".doc") || nm.endsWith(".docx"))
            return "x-office-document";
        if (mt.includes("spreadsheet") || nm.endsWith(".xls") || nm.endsWith(".xlsx"))
            return "x-office-spreadsheet";
        if (mt.includes("presentation") || nm.endsWith(".ppt") || nm.endsWith(".pptx"))
            return "x-office-presentation";
        if (mt === "application/zip" || /\.(zip|rar|7z|gz|tar)$/.test(nm))
            return "application-zip";
        if (mt.startsWith("audio/"))
            return "audio-x-generic";
        if (mt.startsWith("video/"))
            return "video-x-generic";
        if (mt.startsWith("text/"))
            return "text-x-generic";
        return "mail-attachment";
    }
    function inlineAttachmentBorderColorCss() {
        const c = Kirigami.Theme.backgroundColor;
        const luminance = (0.2126 * c.r) + (0.7152 * c.g) + (0.0722 * c.b);
        if (luminance < 0.5)
            return "rgba(255,255,255,0.42)";
        return Qt.darker(c, 2.0).toString();
    }
    function inlineImageAttachmentsHtml(baseHtml) {
        // TODO: gate inline rendering behind a user setting.
        if (!root.attachmentItems || root.attachmentItems.length === 0)
            return "";

        const dataHashes = bodyDataImageHashes(baseHtml);

        const images = [];
        for (let i = 0; i < root.attachmentItems.length; i++) {
            const a = root.attachmentItems[i] || {};
            const mt = (a.mimeType || "").toString().toLowerCase();
            const name = (a.name || "").toString();
            const isImage = mt.startsWith("image/") || /\.(png|jpe?g|webp|gif|bmp|svg)$/i.test(name);
            if (!isImage)
                continue;

            const partId = (a.partId || "").toString();
            const localPath = (root.attachmentLocalPaths && partId.length) ? (root.attachmentLocalPaths[partId] || "") : "";
            if (!localPath || !localPath.length) {
                continue;
            }

            if (dataHashes.length > 0 && appRoot && appRoot.imapServiceObj && appRoot.imapServiceObj.fileSha256) {
                const fileHash = appRoot.imapServiceObj.fileSha256(localPath);
                if (fileHash && fileHash.length && dataHashes.indexOf(fileHash) >= 0) {
                    continue;
                }
            }

            const src = fileUrlFromLocalPath(localPath);
            const alt = (name || i18n("Image attachment")).replace(/"/g, "&quot;");
            images.push("<img src=\"" + src + "\" alt=\"" + alt + "\" style=\"display:block;max-width:100%;height:auto;margin:8px auto 0 auto;\" />");
        }

        if (images.length === 0)
            return "";

        return "<div data-kestrel-inline-attachments='1' style='margin-top:12px;padding-top:8px;border-top:1px solid " + inlineAttachmentBorderColorCss() + ";'>" + images.join("") + "</div>";
    }

    // Returns true if the pixel URL belongs to the sender's own domain (first-party tracking).
    // Compares second-level domain labels: "m.aliexpress.com" vs sender "aliexpress.com" → true.
    // eM Client does not flag first-party pixels; we match that behaviour.
    function isFirstPartyUrl(url, senderDomain) {
        if (!url || !senderDomain)
            return false;
        const urlM = (url || "").match(/^https?:\/\/([^/?#]+)/i);
        if (!urlM)
            return false;
        const urlParts = urlM[1].split(".");
        if (urlParts.length < 2)
            return false;
        const urlSld = urlParts[urlParts.length - 2].toLowerCase();
        const senderParts = senderDomain.toLowerCase().split(".");
        if (senderParts.length < 2)
            return false;
        const senderSld = senderParts[senderParts.length - 2];
        return urlSld === senderSld;
    }

    function normalizedEdgeKey(account, folder, uid) {
        const a = (account || "").toString().toLowerCase();
        const f = (folder || "").toString().toLowerCase();
        const u = (uid || "").toString();
        if (!a.length || !u.length)
            return "";
        return a + "|" + f + "|" + u;
    }
    function reloadAttachmentsForCurrentMessage() {
        root.attachmentLocalPaths = {};
        if (!root.messageData || !appRoot || !appRoot.imapServiceObj) {
            root.attachmentItems = [];
            return;
        }

        const account = (root.messageData.accountEmail || "").toString();
        const folder = (root.messageData.folder || "").toString();
        const uid = (root.messageData.uid || "").toString();
        if (!account.length || !folder.length || !uid.length) {
            root.attachmentItems = [];
            return;
        }

        // Single-shot lookup per message selection; avoid heavy reevaluation via bindings.
        let items = appRoot.imapServiceObj.attachmentsForMessage(account, folder, uid);
        let activeUid = uid;

        // Fallback: selected edge may not be the one that carries attachment metadata
        // in non-Inbox views. Try all known folder/uid edges for this logical message.
        if ((!items || items.length === 0) && appRoot.dataStoreObj && appRoot.dataStoreObj.fetchCandidatesForMessageKey) {
            const candidates = appRoot.dataStoreObj.fetchCandidatesForMessageKey(account, folder, uid) || [];
            for (let i = 0; i < candidates.length; ++i) {
                const c = candidates[i] || {};
                const cf = (c.folder || "").toString();
                const cu = (c.uid || "").toString();
                if (!cf.length || !cu.length)
                    continue;
                const trial = appRoot.imapServiceObj.attachmentsForMessage(account, cf, cu);
                if (trial && trial.length > 0) {
                    items = trial;
                    activeUid = cu;
                    break;
                }
            }
        }

        root.attachmentItems = items || [];

        if (root.attachmentItems.length > 0) {
            // Pre-populate local paths from the 60-min prefetch cache (instant on revisit).
            const paths = {};
            const progress = {};
            const downloading = {};
            for (let i = 0; i < root.attachmentItems.length; i++) {
                const a = root.attachmentItems[i];
                const lp = appRoot.imapServiceObj.cachedAttachmentPath(account, activeUid, a.partId);
                if (lp.length > 0) {
                    paths[a.partId] = lp;
                    progress[a.partId] = 100;
                    downloading[a.partId] = false;
                } else {
                    const existing = Number(root.attachmentProgress[a.partId] || 0);
                    progress[a.partId] = existing > 0 ? existing : 0;
                    downloading[a.partId] = true;
                }
            }
            root.attachmentLocalPaths = paths;
            root.attachmentProgress = progress;
            root.attachmentDownloading = downloading;
        }
    }
    function removeTagByName(name) {
        const tags = activeTags.filter(function (t) {
            return t.name !== name;
        });
        setCurrentTags(tags);
    }
    function renderedHtmlForThread(bodyHtml, darkMode) {
        if (!bodyHtml || !bodyHtml.length)
            return "";
        htmlProcessor.darkBg      = Qt.darker(Kirigami.Theme.backgroundColor, 1.35).toString();
        htmlProcessor.surfaceBg   = Kirigami.Theme.alternateBackgroundColor.toString();
        htmlProcessor.lightText   = Kirigami.Theme.textColor.toString();
        htmlProcessor.borderColor = Kirigami.Theme.disabledTextColor.toString();
        const collapsed = htmlProcessor.collapseBlockquotes(bodyHtml);
        return htmlProcessor.prepareThread(collapsed, darkMode);
    }
    function setCurrentTags(tags) {
        const key = appRoot.selectedMessageKey || "";
        if (!key.length)
            return;
        const next = Object.assign({}, tagMap);
        next[key] = tags;
        tagMap = next;
    }
    function startAllAttachmentPrefetchForCurrentMessage() {
        if (!root.messageData || !appRoot || !appRoot.imapServiceObj)
            return;
        const account = (root.messageData.accountEmail || "").toString();
        const folder = (root.messageData.folder || "").toString();
        const uid = (root.messageData.uid || "").toString();
        if (!account.length || !folder.length || !uid.length)
            return;

        appRoot.imapServiceObj.prefetchAttachments(account, folder, uid);
        if (appRoot.dataStoreObj && appRoot.dataStoreObj.fetchCandidatesForMessageKey) {
            const candidates = appRoot.dataStoreObj.fetchCandidatesForMessageKey(account, folder, uid) || [];
            for (let i = 0; i < candidates.length; ++i) {
                const c = candidates[i] || {};
                const cf = (c.folder || "").toString();
                const cu = (c.uid || "").toString();
                if (!cf.length || !cu.length || (cf === folder && cu === uid))
                    continue;
                appRoot.imapServiceObj.prefetchAttachments(account, cf, cu);
            }
        }
    }
    function startImageAttachmentPrefetchForCurrentMessage() {
        if (!root.messageData || !appRoot || !appRoot.imapServiceObj)
            return;
        const account = (root.messageData.accountEmail || "").toString();
        const folder = (root.messageData.folder || "").toString();
        const uid = (root.messageData.uid || "").toString();
        if (!account.length || !folder.length || !uid.length)
            return;

        appRoot.imapServiceObj.prefetchImageAttachments(account, folder, uid);
        if (appRoot.dataStoreObj && appRoot.dataStoreObj.fetchCandidatesForMessageKey) {
            const candidates = appRoot.dataStoreObj.fetchCandidatesForMessageKey(account, folder, uid) || [];
            for (let i = 0; i < candidates.length; ++i) {
                const c = candidates[i] || {};
                const cf = (c.folder || "").toString();
                const cu = (c.uid || "").toString();
                if (!cf.length || !cu.length || (cf === folder && cu === uid))
                    continue;
                appRoot.imapServiceObj.prefetchImageAttachments(account, cf, cu);
            }
        }
    }
    function threadExpandedBodyHeight(index) {
        // Placeholder — overridden dynamically per card by onContentHeightChanged
        return 400;
    }

    // Parse the DKIM signing domain from an Authentication-Results header and return its SLD.
    // Handles both "header.d=example.com" and "header.i=@example.com" forms.
    function vendorFromAuthResults(authResults) {
        if (!authResults)
            return "";
        const m = (authResults).match(/dkim=pass[^\n]*?header\.(?:d|i)=@?([a-z0-9._-]+)/i);
        return m ? vendorFromUrl("https://" + m[1]) : "";
    }

    // Extract bounce domain from Return-Path (e.g. "<bounce@mg.mailchimp.com>") and
    // return its SLD. Falls back to treating the whole value as a domain string.
    function vendorFromReturnPath(returnPath) {
        if (!returnPath)
            return "";
        const m = returnPath.match(/<[^@>]*@([^>]+)>/);
        const domain = m ? m[1].trim() : returnPath.replace(/[<>\s]/g, "");
        return domain ? vendorFromUrl("https://" + domain) : "";
    }

    // Extract just the second-level domain name from a URL and capitalize it.
    // e.g. "https://opens.mailgun.org/..." → "Mailgun"
    // e.g. "https://images.mailgun.com/..." → "Mailgun"  (not "Images")
    function vendorFromUrl(url) {
        const m = (url || "").match(/^https?:\/\/([^/?#]+)/i);
        if (!m)
            return "";
        const parts = m[1].split(".");
        if (parts.length < 2)
            return "";
        const sld = parts[parts.length - 2];
        return sld.charAt(0).toUpperCase() + sld.slice(1);
    }

    // Extract ESP name from X-Mailer header (first alphabetic word only).
    // e.g. "Mailchimp Mailer" → "Mailchimp", "Klaviyo" → "Klaviyo"
    function vendorFromXMailer(xMailer) {
        const first = ((xMailer || "").trim().match(/^([A-Za-z][A-Za-z0-9]*)/) || [])[1] || "";
        return first ? first.charAt(0).toUpperCase() + first.slice(1) : "";
    }

    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.35)

    onHasUsableBodyHtmlChanged: {
        const len = (root.messageBodyHtml || "").toString().length;
        console.log("[hydrate-html-db] pane-usable-changed", "key=", root.renderMessageKey, "usable=", root.hasUsableBodyHtml, "bodyLen=", len);
    }
    onMessageDataChanged: {
        root.selectedAttachmentKey = "";

        const account = (messageData && messageData.accountEmail) ? messageData.accountEmail.toString() : "";
        const uid = (messageData && messageData.uid) ? messageData.uid.toString() : "";
        const hasIdentity = account.length > 0 && uid.length > 0;
        const messageKey = hasIdentity ? (account + "|" + uid) : "";
        const isSameMessage = hasIdentity && (messageKey === root.lastAttachmentMessageKey);

        if (!hasIdentity) {
            // No message selected — wipe everything.
            root.attachmentItems = [];
            root.attachmentLocalPaths = ({});
            root.attachmentProgress = ({});
            root.attachmentDownloading = ({});
        } else if (!isSameMessage) {
            // Different message — reset state and load fresh.
            root.lastAttachmentMessageKey = messageKey;
            root.attachmentItems = [];
            root.attachmentLocalPaths = ({});
            root.attachmentProgress = ({});
            root.attachmentDownloading = ({});
            root.reloadAttachmentsForCurrentMessage();
            root.startImageAttachmentPrefetchForCurrentMessage();
        } else if (root.attachmentItems.length === 0) {
            // Same message, attachments not yet loaded — retry (race with DB hydration).
            root.reloadAttachmentsForCurrentMessage();
            root.startImageAttachmentPrefetchForCurrentMessage();
        }
        // Same message with attachments already loaded: leave all state untouched.

        // Read messageData directly — derived bindings (isTrustedSender, senderDomain, etc.)
        // may not have re-evaluated yet when this handler fires.
        const senderVal = (messageData && messageData.sender) ? messageData.sender.toString() : "";
        let email = senderVal;
        const angleMatch = senderVal.match(/<([^>]+@[^>]+)>/);
        if (angleMatch)
            email = angleMatch[1];
        const at = email.lastIndexOf('@');
        const domain = at >= 0 ? email.slice(at + 1).trim().toLowerCase() : "";

        const explicitTrust = !!(domain && appRoot && appRoot.dataStoreObj && appRoot.dataStoreObj.isSenderTrusted(domain));

        // Secondary trust signal: DKIM pass in Authentication-Results means the From domain
        // signed the message and it wasn't tampered with in transit.
        const authStr = (messageData && messageData.authResults) ? messageData.authResults.toString() : "";
        const dkimPass = /\bdkim=pass\b/i.test(authStr);
        const dkimDomainMatch = authStr.match(/dkim=pass[^\n]*?header\.(?:d|i)=@?([a-z0-9._-]+)/i);
        const dkimDomain = dkimDomainMatch ? dkimDomainMatch[1] : "";

        if (explicitTrust) {
            imagesAllowed = true;
        } else if (dkimPass) {
            imagesAllowed = true;
        } else {
            imagesAllowed = false;
        }

        trackingAllowed = false;

        // Only start a new transition when the message key changes.
        // Background updates (mark-read, header refreshes) must not restart the animation.
        const key = root.renderMessageKey;
        if (key.length && key !== htmlContainer.pendingKey)
            htmlContainer.startTransition(key);
        // Queue HTML if available (handles case where data arrives before edgeKey).
        htmlContainer.onHtmlUpdate();
    }
    onSelectedMessageEdgeKeyChanged: {
        const key = root.selectedMessageEdgeKey;
        // Always reset thread view paging/expanded state on header selection,
        // even when staying within the same thread id.
        _threadOnSelectedChanged();
        if (key.length && key !== htmlContainer.pendingKey)
            htmlContainer.startTransition(key);
        // Also try to queue HTML now in case messageData is already consistent.
        htmlContainer.onHtmlUpdate();
    }
    onThreadIdChanged: {
        threadShowAll = false;
        threadVisibleCount = 5;
        threadLoadingOlder = false;
        threadScrollEpoch = 0;
        threadExpandedSet = null;
        cardDarkModes = ({});
    }
    onVisibleThreadMessagesChanged: {
        Qt.callLater(function () {
            if (!root.threadLoadingOlder)
                _threadClampScrollToLastCardTop();
            threadScrollEpoch++;
        });
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds

        onTapped: function (eventPoint) {
            if (!attachmentFlow.visible) {
                root.selectedAttachmentKey = "";
                return;
            }
            const p = root.mapToItem(attachmentFlow, eventPoint.position.x, eventPoint.position.y);
            const insideAttachments = p.x >= 0 && p.y >= 0 && p.x <= attachmentFlow.width && p.y <= attachmentFlow.height;
            if (!insideAttachments)
                root.selectedAttachmentKey = "";
        }
    }
    ColumnLayout {
        anchors.bottomMargin: appRoot.sectionSpacing
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: appRoot.sectionSpacing
        spacing: appRoot.sectionSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: !!root.messageData

            QQC2.Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 140
                elide: Text.ElideRight
                font.bold: true
                font.pixelSize: 16
                text: root.messageSubject
            }
            Repeater {
                model: root.activeTags

                delegate: QQC2.Button {
                    required property var modelData

                    flat: true
                    implicitHeight: 28
                    implicitWidth: Math.min(Math.max(52, contentItem.implicitWidth + leftPadding + rightPadding + 8), 150)
                    leftPadding: 10
                    rightPadding: 10
                    text: modelData.name + "  ✕"

                    background: Rectangle {
                        border.color: Qt.darker(modelData.color, 1.08)
                        border.width: 1
                        color: modelData.color
                        radius: height / 2
                    }
                    contentItem: QQC2.Label {
                        color: modelData.textColor
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        maximumLineCount: 1
                        text: parent.text
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: root.removeTagByName(modelData.name)
                }
            }
            QQC2.Button {
                id: deleteButton

                flat: true
                implicitHeight: 28
                implicitWidth: Math.min(150, Math.max(64, contentItem.implicitWidth + leftPadding + rightPadding + 10))
                leftPadding: 10
                rightPadding: 10
                text: root.folderName + "  ✕"

                background: Rectangle {
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.35)
                    border.width: 1
                    color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.1)
                    radius: height / 2
                }
                contentItem: QQC2.Label {
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: appRoot.deleteSelectedMessages()

                TextToolTip {
                    parent: QQC2.Overlay.overlay
                    preferredX: deleteButton.mapToItem(QQC2.Overlay.overlay, Math.round((deleteButton.width - implicitWidth) / 2), 0).x
                    preferredY: deleteButton.mapToItem(QQC2.Overlay.overlay, Math.round((deleteButton.height - implicitHeight) / 2), 0).y - implicitHeight - 6
                    toolTipText: i18n("Delete this message")
                    visible: deleteButton.hovered
                }
            }
            QQC2.ToolButton {
                id: tagMenuButton

                Layout.rightMargin: -2
                display: QQC2.AbstractButton.IconOnly
                icon.name: "tag"

                onClicked: tagMenu.openIfClosed()
            }
            PopupMenu {
                id: tagMenu

                parent: tagMenuButton
                verticalOffset: 4

                QQC2.MenuItem {
                    text: i18n("Draft")

                    onTriggered: root.addTag({
                        name: i18n("Draft"),
                        color: "#E0E0E0",
                        textColor: "#2B2B2B"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Junk")

                    onTriggered: root.addTag({
                        name: i18n("Junk"),
                        color: "#FFE2D8",
                        textColor: "#7A2E16"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Unwanted")

                    onTriggered: root.addTag({
                        name: i18n("Unwanted"),
                        color: "#F9D7DE",
                        textColor: "#7A1F34"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Important")

                    onTriggered: root.addTag({
                        name: i18n("Important"),
                        color: "#FFEAAE",
                        textColor: "#6A4A00"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Home")

                    onTriggered: root.addTag({
                        name: i18n("Home"),
                        color: "#D9F0FF",
                        textColor: "#114D73"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Newsletter")

                    onTriggered: root.addTag({
                        name: i18n("Newsletter"),
                        color: "#E8E2FF",
                        textColor: "#473088"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Personal")

                    onTriggered: root.addTag({
                        name: i18n("Personal"),
                        color: "#E3FBD9",
                        textColor: "#2D6A23"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Promotion")

                    onTriggered: root.addTag({
                        name: i18n("Promotion"),
                        color: "#D6E8FF",
                        textColor: "#1D4E89"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("School")

                    onTriggered: root.addTag({
                        name: i18n("School"),
                        color: "#DCE8FF",
                        textColor: "#1B3A73"
                    })
                }
                QQC2.MenuItem {
                    text: i18n("Work")

                    onTriggered: root.addTag({
                        name: i18n("Work"),
                        color: "#FFE9C9",
                        textColor: "#7A4B08"
                    })
                }
                QQC2.MenuSeparator {
                }
                QQC2.MenuItem {
                    text: i18n("New tag…")

                    onTriggered: customTagDialog.open()
                }
            }
        }
        RowLayout {
            // Layout.bottomMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing * 2
            Layout.fillWidth: true
            Layout.topMargin: 28
            spacing: Kirigami.Units.smallSpacing
            visible: !!root.messageData && !root.isThreadView

            Item {
                id: avatarWrap

                property var avatarSources: appRoot.senderAvatarSources(root.senderText, "", "", root.messageData && root.messageData.accountEmail ? root.messageData.accountEmail : "")

                Layout.preferredHeight: Kirigami.Units.iconSizes.large + 8
                Layout.preferredWidth: Kirigami.Units.iconSizes.large + 8

                AvatarBadge {
                    anchors.centerIn: parent
                    avatarSources: avatarWrap.avatarSources
                    displayName: root.senderName
                    fallbackText: root.senderText
                    size: Kirigami.Units.iconSizes.large + 8
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                QQC2.Label {
                    id: senderLink

                    Layout.fillWidth: false
                    color: "#4ea3ff"
                    elide: Text.ElideRight
                    font.bold: true
                    font.pixelSize: 13
                    text: root.senderName

                    MouseArea {
                        id: senderMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: appRoot.openComposerTo(root.senderEmail, i18n("sender"))
                    }
                    ContactHoverPopup {
                        id: senderPopup

                        anchorItem: senderLink
                        avatarSources: avatarWrap.avatarSources
                        avatarText: appRoot.avatarInitials(root.senderName)
                        primaryButtonText: i18n("Send mail")
                        secondaryButtonText: i18n("Add to contacts")
                        subtitleText: root.senderEmail
                        targetHovered: senderMouse.containsMouse
                        titleText: root.senderName

                        onPrimaryTriggered: appRoot.openComposerTo(root.senderEmail, i18n("tooltip send"))
                        onSecondaryTriggered: appRoot.showInlineStatus(i18n("Add to contacts requested for %1").arg(root.senderEmail), false)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    QQC2.Label {
                        color: Kirigami.Theme.textColor
                        opacity: 0.9
                        text: i18n("to")
                    }
                    QQC2.Label {
                        id: recipientLink

                        Layout.fillWidth: false
                        color: "#4ea3ff"
                        elide: Text.ElideRight
                        font.bold: true
                        font.pixelSize: 13
                        opacity: 0.95
                        text: root.recipientName

                        MouseArea {
                            id: recipientMouse

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true

                            onClicked: appRoot.openComposerTo(root.recipientsText, i18n("recipient"))
                        }
                        ContactHoverPopup {
                            id: recipientPopup

                            anchorItem: recipientLink
                            avatarSources: root.recipientAvatarSources
                            avatarText: (root.recipientName || "?").slice(0, 1).toUpperCase()
                            primaryButtonText: i18n("Send mail")
                            secondaryButtonText: i18n("Add to contacts")
                            subtitleText: root.recipientsText
                            targetHovered: recipientMouse.containsMouse
                            titleText: root.recipientName

                            onPrimaryTriggered: appRoot.openComposerTo(root.recipientsText, i18n("recipient"))
                            onSecondaryTriggered: appRoot.showInlineStatus(i18n("Add to contacts requested for %1").arg(root.recipientsText), false)
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 2

                QQC2.Label {
                    opacity: 0.82
                    text: root.receivedAtText
                }
                RowLayout {
                    spacing: 6

                    MailActionButton {
                        iconName: "mail-reply-sender"
                        menuItems: [
                            {
                                text: i18n("Reply"),
                                icon: "mail-reply-sender"
                            },
                            {
                                text: i18n("Reply all"),
                                icon: "mail-reply-all"
                            },
                            {
                                text: i18n("Forward"),
                                icon: "mail-forward"
                            }
                        ]
                        text: i18n("Reply")

                        onTriggered: function (actionText) {
                            if (actionText === i18n("Reply") || actionText.length === 0) {
                                appRoot.composeDraftSubject = i18n("Re: %1").arg(root.messageSubject);
                                appRoot.openComposerTo(root.senderEmail, i18n("reply"));
                            } else if (actionText === i18n("Reply all")) {
                                appRoot.composeDraftSubject = i18n("Re: %1").arg(root.messageSubject);
                                appRoot.openComposerTo(root.recipientsText, i18n("reply all"));
                            } else if (actionText === i18n("Forward")) {
                                appRoot.composeDraftTo = "";
                                appRoot.composeDraftSubject = i18n("Fwd: %1").arg(root.messageSubject);
                                appRoot.composeDraftBody = i18n("\n\n--- Forwarded message ---\nFrom: %1\nDate: %2\n\n%3").arg(root.senderText).arg(root.receivedAtText).arg((root.messageData && root.messageData.snippet) ? root.messageData.snippet : "");
                                appRoot.openComposeDialog(i18n("forward"));
                            }
                        }
                    }
                    QQC2.Button {
                        icon.name: root.forceDarkHtml ? "weather-clear-night" : "weather-clear"
                        text: root.forceDarkHtml ? i18n("Dark") : i18n("Light")

                        onClicked: {
                            if (appRoot)
                                appRoot.contentPaneDarkModeEnabled = !appRoot.contentPaneDarkModeEnabled;
                        }
                    }
                }
            }
        }

        // Info bars — outer layout is visible when any bar has something to show.
        ColumnLayout {
            id: infoBars

            Layout.bottomMargin: Kirigami.Units.largeSpacing * 2
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            visible: !root.isThreadView && !!root.messageData && ((root.hasExternalImages && !root.imagesAllowed && !root.isTrustedSender) || (root.hasTrackingPixel && !root.trackingAllowed && root.imagesAllowed) || root.listUnsubscribeUrl.length > 0)

            // Unsubscribe Info Bar
            RowLayout {
                id: unsubInfo

                Layout.alignment: Qt.AlignLeft
                spacing: Kirigami.Units.smallSpacing
                visible: !!root.messageData && root.listUnsubscribeUrl.length > 0

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignLeft
                    Layout.preferredHeight: 16
                    Layout.preferredWidth: 16
                    source: "help-contextual"
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: "#4ea3ff"
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("Unsubscribe")

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: {
                            Qt.openUrlExternally(root.listUnsubscribeUrl);
                        }
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("from receiving these messages")
                }
            }

            // Tracker Info Bar
            RowLayout {
                id: trackerInfo

                Layout.alignment: Qt.AlignLeft
                spacing: Kirigami.Units.smallSpacing
                visible: !!root.messageData && root.hasTrackingPixel && !root.trackingAllowed && root.imagesAllowed

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignLeft
                    Layout.preferredHeight: 16
                    Layout.preferredWidth: 16
                    source: "crosshairs"
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: "#4ea3ff"
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("Allow email tracking.")

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: {
                            root.trackingAllowed = true;
                        }
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: Kirigami.Theme.textColor
                    // text: i18n("email tracking was blocked to preserve privacy.")
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: (root.trackerVendor || "Email") + i18n(" tracking was blocked to preserve privacy.")
                }
            }

            // Images Info Bar — hidden when a tracking pixel is present (tracker bar takes priority)
            RowLayout {
                id: downloadInfo

                Layout.alignment: Qt.AlignLeft
                spacing: Kirigami.Units.smallSpacing
                visible: !!root.messageData && root.hasExternalImages && !root.imagesAllowed && !root.isTrustedSender

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignLeft
                    Layout.preferredHeight: 16
                    Layout.preferredWidth: 16
                    source: "messagebox_warning"
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: "#4ea3ff"
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("Download Pictures")

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: {
                            root.imagesAllowed = true;
                        }
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("or")
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: "#4ea3ff"
                    font.bold: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("always download pictures from this sender.")

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: {
                            if (root.senderDomain && appRoot && appRoot.dataStoreObj)
                                appRoot.dataStoreObj.setTrustedSenderDomain(root.senderDomain);
                            root.imagesAllowed = true;
                        }
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: false
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                    text: i18n("To preserve privacy, external content was not downloaded.")
                }
            }
        }
        Flow {
            id: attachmentFlow

            Layout.bottomMargin: visible ? Kirigami.Units.largeSpacing : 0
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: !!root.messageData && root.attachmentItems.length > 0

            Repeater {
                model: root.attachmentItems

                delegate: Rectangle {
                    id: attachmentCard

                    readonly property string localPath: (root.attachmentLocalPaths && root.attachmentLocalPaths[attachmentCard.modelData.partId]) ? root.attachmentLocalPaths[attachmentCard.modelData.partId] : ""
                    required property var modelData
                    readonly property string previewSource: {
                        if (!localPath.length || !root.messageData || !appRoot || !appRoot.imapServiceObj)
                            return "";
                        return appRoot.imapServiceObj.attachmentPreviewPath(root.messageData.accountEmail, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.mimeType || "");
                    }
                    readonly property bool selected: root.selectedAttachmentKey === attachmentCard.modelData.partId
                    readonly property string sizeText: root.formatAttachmentSize(attachmentCard.modelData.bytes)

                    border.color: selected ? root.systemPalette.highlight : Qt.lighter(Kirigami.Theme.backgroundColor, 1.35)
                    border.width: 1
                    color: selected ? Qt.lighter(root.systemPalette.highlight, 1.1) : Qt.lighter(Kirigami.Theme.backgroundColor, 1.12)
                    height: 28
                    radius: height / 2
                    width: cardRow.implicitWidth + 18

                    RowLayout {
                        id: cardRow

                        anchors.left: parent.left
                        anchors.leftMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Kirigami.Icon {
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 16
                            source: root.iconForAttachment(attachmentCard.modelData.mimeType, attachmentCard.modelData.name)
                        }
                        QQC2.Label {
                            elide: Text.ElideRight
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                            maximumLineCount: 1
                            text: attachmentCard.modelData.name
                        }
                        QQC2.Label {
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
                            opacity: 0.65
                            text: "(" + attachmentCard.sizeText + ")"
                            visible: attachmentCard.sizeText.length > 0
                        }
                    }
                    HoverHandler {
                        id: attachmentHover

                        onHoveredChanged: if (hovered)
                            root.startAllAttachmentPrefetchForCurrentMessage()
                    }
                    MouseArea {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor

                        onClicked: function (mouse) {
                            root.startAllAttachmentPrefetchForCurrentMessage();
                            if (mouse.button === Qt.LeftButton) {
                                root.selectedAttachmentKey = attachmentCard.modelData.partId;
                            } else if (mouse.button === Qt.RightButton) {
                                root.selectedAttachmentKey = attachmentCard.modelData.partId;
                                attachmentMenu.popup();
                            }
                        }
                        onDoubleClicked: function (mouse) {
                            if (mouse.button !== Qt.LeftButton)
                                return;
                            root.startAllAttachmentPrefetchForCurrentMessage();
                            appRoot.imapServiceObj.openAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding);
                        }
                    }
                    QQC2.Menu {
                        id: attachmentMenu

                        QQC2.MenuItem {
                            text: i18n("Open Attachment")

                            onTriggered: appRoot.imapServiceObj.openAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding)
                        }
                        QQC2.MenuItem {
                            text: i18n("Save as…")

                            onTriggered: appRoot.imapServiceObj.saveAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding)
                        }
                        QQC2.MenuItem {
                            text: i18n("Forward...")
                        }
                        QQC2.MenuItem {
                            text: i18n("Copy")
                        }
                        QQC2.MenuItem {
                            text: i18n("Remove")
                        }
                        QQC2.MenuSeparator {
                        }
                        QQC2.MenuItem {
                            text: i18n("Select All")
                        }
                        QQC2.MenuItem {
                            text: i18n("Save All Attachments")
                        }
                        QQC2.MenuItem {
                            text: i18n("Open All Attachments")
                        }
                    }
                    AttachmentHoverPopup {
                        anchorItem: attachmentCard
                        arrowLeftPx: Math.max(24, attachmentCard.width * 0.25)
                        downloadComplete: Number(root.attachmentProgress[attachmentCard.modelData.partId] || 0) >= 100
                        downloadProgress: Number(root.attachmentProgress[attachmentCard.modelData.partId] || 0)
                        fallbackIcon: root.iconForAttachment(attachmentCard.modelData.mimeType, attachmentCard.modelData.name)
                        openButtonText: i18n("Open")
                        previewMimeType: (attachmentCard.modelData.mimeType || "")
                        previewSource: attachmentCard.previewSource
                        saveButtonText: i18n("Save")
                        targetHovered: attachmentHover.hovered || attachmentCard.selected

                        onOpenTriggered: appRoot.imapServiceObj.openAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding)
                        onSaveTriggered: appRoot.imapServiceObj.saveAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding)
                    }
                }
            }
        }
        // ── Thread view ──────────────────────────────────────────────────────
        QQC2.Button {
            id: showOlderFloatingBtn

            Layout.alignment: Qt.AlignHCenter
            bottomPadding: 6
            flat: true
            leftPadding: 16
            rightPadding: 16
            text: i18n("Show %1 older message(s)", root.threadOffTopCount)
            topPadding: 6
            visible: root.isThreadView && root.threadOffTopCount > 0

            background: Rectangle {
                border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.55)
                border.width: 1
                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                radius: height / 2
            }
            contentItem: QQC2.Label {
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                text: parent.text
                verticalAlignment: Text.AlignVCenter
            }

            onClicked: {
                if (root.threadHiddenCount > 0)
                    root._threadLoadOlder(Math.min(12, root.threadHiddenCount));
                else
                    threadFlickable.contentY = Math.max(0, threadFlickable.contentY - Math.max(120, threadFlickable.height * 0.75));
            }
        }
        Item {
            id: threadFlickableWa

            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.rightMargin: -20
            visible: root.isThreadView

            Flickable {
                id: threadFlickable

                QQC2.ScrollBar.vertical: threadVScroll
                anchors.fill: parent
                anchors.rightMargin: threadVScroll.width + Kirigami.Units.largeSpacing * 2
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                contentHeight: Math.max(height + 1, threadScrollContent.implicitHeight)
                contentWidth: width

                onContentYChanged: {
                    if (contentY <= 24 && root.threadHiddenCount > 0 && !root.threadLoadingOlder)
                        root._threadLoadOlder(6);
                    if (!root.threadLoadingOlder)
                        root._threadClampScrollToLastCardTop();
                    root.threadScrollEpoch++;
                }
                onHeightChanged: root._threadClampScrollToLastCardTop()
                onMovementEnded: root._threadClampScrollToLastCardTop()

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    grabPermissions: PointerHandler.CanTakeOverFromAnything

                    onWheel: function (ev) {
                        const delta = ev.angleDelta ? ev.angleDelta.y : 0;
                        if (!delta)
                            return;
                        const next = threadFlickable.contentY - (delta / 2);
                        // Scrolling up past contentY=0 with hidden older messages: load them
                        if (next < 0 && root.threadHiddenCount > 0 && !root.threadLoadingOlder)
                            root._threadLoadOlder(6);
                        threadFlickable.contentY = Math.max(0, Math.min(next, root._threadMaxContentY()));
                        ev.accepted = true;
                    }
                }
                Column {
                    id: threadScrollContent

                    bottomPadding: 12
                    spacing: 8
                    topPadding: 0
                    width: threadFlickable.width

                    // Message cards
                    Repeater {
                        id: threadCardsRepeater

                        model: root.visibleThreadMessages

                        delegate: Rectangle {
                            id: threadCard

                            property real bodyHeight: 24
                            readonly property string cardBodyHtml: root._threadBodyTextForCard(index)
                            required property int index
                            readonly property bool isExpanded: root._threadIsExpanded(index)
                            required property var modelData

                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
                            border.width: 1
                            color: isExpanded ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35) : Kirigami.Theme.backgroundColor
                            height: implicitHeight
                            implicitHeight: cardHeaderRow.implicitHeight + 32 + (isExpanded ? bodyHeight + 12 : 0)
                            radius: 8
                            width: threadScrollContent.width

                            onIsExpandedChanged: {
                                if (isExpanded)
                                    root._threadOnCardExpanded(index);
                            }

                            // ── Card header ───────────────────────────────────────────
                            MouseArea {
                                cursorShape: Qt.PointingHandCursor
                                enabled: true
                                height: threadCard.isExpanded ? (cardHeaderRow.y + cardHeaderRow.implicitHeight + 8) : parent.height
                                propagateComposedEvents: true
                                width: parent.width
                                x: 0
                                y: 0

                                onClicked: root._threadToggleExpand(threadCard.index)
                            }
                            RowLayout {
                                id: cardHeaderRow

                                spacing: 10
                                width: parent.width - 20
                                x: 10
                                y: threadCard.isExpanded ? 8 : Math.max(8, Math.round((threadCard.height - implicitHeight) / 2))

                                // Avatar
                                Item {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredHeight: 40
                                    Layout.preferredWidth: 40

                                    AvatarBadge {
                                        anchors.fill: parent
                                        avatarSources: root._threadAvatarSources(threadCard.modelData)
                                        displayName: root._threadSenderName(threadCard.modelData)
                                        fallbackText: threadCard.modelData.sender || ""
                                        size: 40
                                    }
                                }

                                // Sender / snippet / recipient
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    spacing: 2

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        color: "#4ea3ff"
                                        elide: Text.ElideRight
                                        font.bold: true
                                        font.pixelSize: 13
                                        text: root._threadSenderName(threadCard.modelData)
                                    }

                                    // Collapsed: snippet
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        opacity: 0.72
                                        text: threadCard.modelData.snippet || ""
                                        visible: !threadCard.isExpanded
                                    }

                                    // Expanded: recipient line
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        visible: threadCard.isExpanded

                                        QQC2.Label {
                                            font.pixelSize: 12
                                            opacity: 0.8
                                            text: i18n("to")
                                        }
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            color: "#4ea3ff"
                                            elide: Text.ElideRight
                                            font.bold: true
                                            font.pixelSize: 12
                                            text: root._threadRecipientName(threadCard.modelData)
                                        }
                                    }
                                }

                                // Date + action buttons
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 4

                                    QQC2.Label {
                                        Layout.alignment: Qt.AlignRight
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignRight
                                        opacity: 0.75
                                        text: root._threadDate(threadCard.modelData)
                                    }
                                    RowLayout {
                                        spacing: 4
                                        visible: true

                                        MailActionButton {
                                            iconName: "mail-reply-sender"
                                            menuItems: [
                                                {
                                                    text: i18n("Reply"),
                                                    icon: "mail-reply-sender"
                                                },
                                                {
                                                    text: i18n("Reply all"),
                                                    icon: "mail-reply-all"
                                                },
                                                {
                                                    text: i18n("Forward"),
                                                    icon: "mail-forward"
                                                }
                                            ]
                                            text: i18n("Reply")

                                            onTriggered: function (actionText) {
                                                const msg = threadCard.modelData;
                                                if (!msg)
                                                    return;
                                                if (actionText === i18n("Reply all")) {
                                                    appRoot.composeDraftSubject = i18n("Re: %1").arg(root.messageSubject);
                                                    appRoot.openComposerTo(msg.recipient || "", i18n("reply all"));
                                                } else if (actionText === i18n("Forward")) {
                                                    appRoot.composeDraftTo = "";
                                                    appRoot.composeDraftSubject = i18n("Fwd: %1").arg(root.messageSubject);
                                                    appRoot.composeDraftBody = i18n("\n\n--- Forwarded message ---\nFrom: %1\nDate: %2\n\n%3").arg(msg.sender || "").arg(root._threadDate(msg)).arg(msg.snippet || "");
                                                    appRoot.openComposeDialog(i18n("forward"));
                                                } else {
                                                    appRoot.composeDraftSubject = i18n("Re: %1").arg(root.messageSubject);
                                                    appRoot.openComposerTo(msg.replyTo || msg.sender || "", i18n("reply"));
                                                }
                                            }
                                        }
                                        QQC2.Button {
                                            icon.name: root._threadCardDarkMode(threadCard.index) ? "weather-clear-night" : "weather-clear"
                                            text: root._threadCardDarkMode(threadCard.index) ? i18n("Dark") : i18n("Light")

                                            onClicked: root._threadToggleCardDark(threadCard.index)
                                        }
                                    }
                                }
                            }

                            // ── Expanded body ─────────────────────────────────────────
                            Loader {
                                id: bodyLoader

                                active: threadCard.isExpanded
                                height: threadCard.bodyHeight
                                width: threadCard.width - 20
                                x: 10
                                y: cardHeaderRow.implicitHeight + 16

                                sourceComponent: Component {
                                    Item {
                                        function loadHtmlDoc(html) {
                                            threadBodyView.loadHtml(html, "file:///");
                                        }

                                        height: bodyLoader.height
                                        width: bodyLoader.width

                                        WebEngineView {
                                            id: threadBodyView

                                            anchors.fill: parent
                                            backgroundColor: Kirigami.Theme.backgroundColor
                                            settings.autoLoadImages: true
                                            settings.javascriptEnabled: false
                                            settings.localContentCanAccessRemoteUrls: true

                                            Component.onCompleted: {
                                                const html = threadCard.cardBodyHtml;
                                                if (html.length)
                                                    loadHtml(html, "file:///");
                                            }
                                            onContentsSizeChanged: {
                                                const h = Number(contentsSize.height);
                                                const target = Math.max(24, isFinite(h) && h >= 0 ? h : 24);
                                                if (Math.abs(target - threadCard.bodyHeight) > 0.5) {
                                                    threadCard.bodyHeight = target;
                                                    root.threadScrollEpoch++;
                                                }
                                            }
                                            onLoadingChanged: function (req) {
                                                if (req.status === WebEngineLoadingInfo.LoadSucceededStatus) {
                                                    runJavaScript("document.documentElement.style.overflow='hidden';document.body.style.overflow='hidden';");
                                                    const h = Number(contentsSize.height);
                                                    const target = Math.max(24, isFinite(h) && h >= 0 ? h : 24);
                                                    if (Math.abs(target - threadCard.bodyHeight) > 0.5) {
                                                        threadCard.bodyHeight = target;
                                                        root.threadScrollEpoch++;
                                                    }
                                                }
                                            }
                                            onNavigationRequested: function (request) {
                                                const url = request.url ? request.url.toString() : "";
                                                if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation && (url.startsWith("http://") || url.startsWith("https://"))) {
                                                    request.action = WebEngineNavigationRequest.IgnoreRequest;
                                                    Qt.openUrlExternally(url);
                                                }
                                            }
                                            onNewWindowRequested: function (request) {
                                                const url = request.requestedUrl ? request.requestedUrl.toString() : "";
                                                if (url.startsWith("http://") || url.startsWith("https://"))
                                                    Qt.openUrlExternally(url);
                                            }
                                        }

                                        // Hard wheel-capture layer so thread pane scroll works even when cursor
                                        // is over WebEngine content (which otherwise consumes wheel input).
                                        Item {
                                            anchors.fill: parent
                                            z: 999

                                            WheelHandler {
                                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                                grabPermissions: PointerHandler.CanTakeOverFromAnything

                                                onWheel: function (ev) {
                                                    const delta = ev.angleDelta ? ev.angleDelta.y : 0;
                                                    if (!delta)
                                                        return;
                                                    const next = threadFlickable.contentY - (delta / 2);
                                                    threadFlickable.contentY = Math.max(0, Math.min(next, root._threadMaxContentY()));
                                                    ev.accepted = true;
                                                }
                                            }
                                        }
                                    }
                                }

                                onLoaded: {
                                    // reload when HTML changes (e.g., after hydration arrives)
                                }

                                WheelHandler {
                                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                    grabPermissions: PointerHandler.CanTakeOverFromAnything

                                    onWheel: function (ev) {
                                        const delta = ev.angleDelta ? ev.angleDelta.y : 0;
                                        if (!delta)
                                            return;
                                        const next = threadFlickable.contentY - (delta / 2);
                                        threadFlickable.contentY = Math.max(0, Math.min(next, root._threadMaxContentY()));
                                        ev.accepted = true;
                                    }
                                }
                            }

                            // Watch for body HTML becoming available after hydration
                            Connections {
                                function onCardBodyHtmlChanged() {
                                    if (!threadCard.isExpanded)
                                        return;
                                    const html = threadCard.cardBodyHtml;
                                    if (!html.length)
                                        return;
                                    if (bodyLoader.item && bodyLoader.item.loadHtmlDoc)
                                        bodyLoader.item.loadHtmlDoc(html);
                                }

                                target: threadCard
                            }
                        }
                    }
                }
            }
            QQC2.ScrollBar {
                id: threadVScroll

                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.top: parent.top
                policy: QQC2.ScrollBar.AsNeeded
                visible: false
                width: 5
            }
        }

        // ── Single message view ──────────────────────────────────────────────
        Rectangle {
            id: htmlContainer

            // ── Fade & load state ─────────────────────────────────────────────
            property real bodyOpacity: 1.0
            // True once the 250 ms fade-out animation has completed.
            property bool fadedOut: false
            property real flickVelocityY: 0
            // Dedup key: prevents re-loading the same content twice.
            property string lastHtmlKey: ""
            // Key of the message currently being loaded by the WebEngineView.
            property string loadingKey: ""
            // HTML queued for pendingKey (empty until renderedHtml is available).
            property string pendingHtml: ""

            // Key of the message we are transitioning to show.
            property string pendingKey: ""

            // ── Hand the queued HTML to the WebEngineView ─────────────────────
            function doLoad() {
                if (!pendingHtml.length || pendingKey !== root.selectedMessageEdgeKey)
                    return;
                loadingKey = pendingKey;
                htmlView.loadHtml(pendingHtml, "file:///");
            }

            // ── Called whenever renderedHtml or imagesAllowed changes ─────────
            function onHtmlUpdate() {
                const key = root.renderMessageKey;
                const html = root.renderedHtml;
                if (!html.length || !key.length)
                    return;
                // Wait until messageData is consistent with the selection.
                if (root.selectedMessageEdgeKey.length > 0 && key !== root.selectedMessageEdgeKey)
                    return;

                const dedup = key + "|" + (root.imagesAllowed ? "1" : "0") + "|" + html;
                if (dedup === lastHtmlKey)
                    return; // already loaded or queued this exact content

                if (key !== pendingKey) {
                    // HTML arrived for a message that has no transition in progress yet
                    // (late data or refresh of currently-shown message).
                    startTransition(key);
                }

                lastHtmlKey = dedup;
                pendingHtml = html;
                if (fadedOut || !fadeTimer.running)
                    doLoad();
            // If fading out: fadeTimer.onTriggered will call doLoad() once the
            // fade-out animation completes.
            }

            // ── Scroll the web view programmatically ──────────────────────────
            function scrollHtmlBy(deltaY) {
                if (!root.hasUsableBodyHtml)
                    return;
                const amount = Number(deltaY);
                if (!isFinite(amount) || Math.abs(amount) < 0.01)
                    return;
                htmlView.runJavaScript("window.scrollBy(0," + Math.round(amount) + ");");
            }

            // ── Begin an animated transition to a new message ────────────────
            // Only call this when the message KEY changes. Background updates
            // (mark-read, hydration result, etc.) go through onHtmlUpdate().
            function startTransition(key) {
                pendingKey = key;
                pendingHtml = "";
                lastHtmlKey = "";
                fadedOut = false;
                loadingKey = "";
                bodyOpacity = 0.0;     // triggers 250 ms NumberAnimation
                fadeTimer.restart();
            }

            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "transparent"
            visible: !root.isThreadView && !!root.messageData

            Behavior on bodyOpacity {
                NumberAnimation {
                    duration: 250
                    easing.type: Easing.InOutQuad
                }
            }

            // Fires when the 250 ms fade-out animation has completed.
            Timer {
                id: fadeTimer

                interval: 250
                repeat: false

                onTriggered: {
                    if (htmlContainer.pendingKey !== root.selectedMessageEdgeKey)
                        return;
                    htmlContainer.fadedOut = true;
                    if (htmlContainer.pendingHtml.length)
                        htmlContainer.doLoad();
                    // If HTML not yet available, doLoad() will be called from onHtmlUpdate()
                    // once renderedHtml becomes non-empty.
                }
            }
            WebEngineView {
                id: htmlView

                anchors.fill: parent
                backgroundColor: root.forceDarkHtml ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35) : "white"
                opacity: htmlContainer.bodyOpacity
                settings.autoLoadImages: true
                settings.errorPageEnabled: true
                settings.localContentCanAccessFileUrls: true
                settings.localContentCanAccessRemoteUrls: true
                visible: root.hasUsableBodyHtml

                Component.onCompleted: htmlContainer.onHtmlUpdate()
                onLoadingChanged: function (req) {
                    const st = req.status;
                    if (st !== WebEngineLoadingInfo.LoadSucceededStatus && st !== WebEngineLoadingInfo.LoadFailedStatus)
                        return;
                    if (htmlContainer.loadingKey !== root.selectedMessageEdgeKey) {
                        htmlContainer.loadingKey = "";
                        return;
                    }
                    htmlContainer.loadingKey = "";
                    htmlContainer.pendingHtml = "";
                    htmlContainer.fadedOut = false;
                    htmlContainer.bodyOpacity = 1.0;
                }
                onNavigationRequested: function (request) {
                    const url = request.url ? request.url.toString() : "";
                    if (!url.length)
                        return;
                    if (url.toLowerCase().startsWith("mailto:")) {
                        request.action = WebEngineNavigationRequest.IgnoreRequest;
                        if (!root.handleMailtoUrl(url)) {
                            Qt.openUrlExternally(url);
                        }
                        return;
                    }

                    if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation && (url.startsWith("http://") || url.startsWith("https://"))) {
                        request.action = WebEngineNavigationRequest.IgnoreRequest;
                        Qt.openUrlExternally(url);
                    }
                }

                // Links with target="_blank" fire onNewWindowRequested instead of
                // onNavigationRequested — handle them the same way.
                onNewWindowRequested: function (request) {
                    const url = request.requestedUrl ? request.requestedUrl.toString() : "";
                    if (url.startsWith("http://") || url.startsWith("https://"))
                        Qt.openUrlExternally(url);
                    else if (url.toLowerCase().startsWith("mailto:"))
                        Qt.openUrlExternally(url);
                }

                Connections {
                    function onImagesAllowedChanged() {
                        htmlContainer.onHtmlUpdate();
                    }
                    function onRenderedHtmlChanged() {
                        htmlContainer.onHtmlUpdate();
                    }

                    target: root
                }
            }
        }
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
            visible: !root.messageData

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignHCenter
                    height: 42
                    source: "mail-message"
                    width: 42
                }
                QQC2.Label {
                    Layout.alignment: Qt.AlignHCenter
                    font.bold: true
                    text: i18n("Select a message")
                }
                QQC2.Label {
                    Layout.alignment: Qt.AlignHCenter
                    opacity: 0.75
                    text: i18n("Choose an email from the list to view its content.")
                }
            }
        }
    }
    Column {
        id: inlineMessageStack

        anchors.bottom: parent.bottom
        anchors.bottomMargin: appRoot.sectionSpacing
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.right: parent.right
        anchors.rightMargin: 20
        spacing: 6
        z: 20

        Repeater {
            model: appRoot.inlineStatusQueue || []

            delegate: Kirigami.InlineMessage {
                id: inlineMsg

                property bool closing: false
                required property var modelData

                opacity: closing ? 0 : 1
                showCloseButton: true
                text: modelData.text || ""
                type: modelData.isError ? Kirigami.MessageType.Error : Kirigami.MessageType.Positive
                visible: true
                width: inlineMessageStack.width

                Behavior on opacity {
                    NumberAnimation {
                        duration: 220
                    }
                }

                onLinkActivated: Qt.openUrlExternally(link)
                onVisibleChanged: {
                    if (!visible) {
                        appRoot.dismissInlineStatus(modelData.id);
                    }
                }

                Timer {
                    interval: 5000
                    repeat: false
                    running: true

                    onTriggered: inlineMsg.closing = true
                }
                Timer {
                    interval: 220
                    repeat: false
                    running: inlineMsg.closing

                    onTriggered: appRoot.dismissInlineStatus(inlineMsg.modelData.id)
                }
            }
        }
    }
    Connections {
        function onAttachmentDownloadProgress(accountEmail, uid, partId, progressPercent) {
            // console.log("[attachment-progress] signal",
            //             "account=", accountEmail,
            //             "uid=", uid,
            //             "partId=", partId,
            //             "progress=", progressPercent)

            if (!root.messageData)
                return;
            if (accountEmail !== root.messageData.accountEmail || uid !== root.messageData.uid)
                return;
            const prev = Number(root.attachmentProgress[partId] || 0);
            const next = (progressPercent === 0 && prev > 0 && prev < 100) ? prev : Math.max(prev, progressPercent);

            const p = Object.assign({}, root.attachmentProgress);
            p[partId] = next;
            root.attachmentProgress = p;

            const d = Object.assign({}, root.attachmentDownloading);
            d[partId] = next < 100;
            root.attachmentDownloading = d;
        }
        function onAttachmentReady(accountEmail, uid, partId, localPath) {
            if (!root.messageData)
                return;
            if (accountEmail !== root.messageData.accountEmail || uid !== root.messageData.uid)
                return;
            const updated = Object.assign({}, root.attachmentLocalPaths);
            updated[partId] = localPath;
            root.attachmentLocalPaths = updated;

            const p = Object.assign({}, root.attachmentProgress);
            p[partId] = 100;
            root.attachmentProgress = p;

            const d = Object.assign({}, root.attachmentDownloading);
            d[partId] = false;
            root.attachmentDownloading = d;
        }

        target: appRoot ? appRoot.imapServiceObj : null
    }
    QQC2.Dialog {
        id: customTagDialog

        property string pendingTagName: ""

        modal: true
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        title: i18n("Create tag")
        width: 320

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
            const name = pendingTagName.trim();
            if (!name.length)
                return;
            root.addTag({
                name: name,
                color: "#E6EEF8",
                textColor: "#1E3C5A"
            });
            pendingTagName = "";
        }
        onRejected: pendingTagName = ""
    }
}
