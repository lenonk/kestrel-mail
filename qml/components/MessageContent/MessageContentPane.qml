import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import ".."
import "../Attachments"

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
    property string attachmentSourceUid: ""
    property string attachmentSourceFolder: ""

    readonly property string folderName: {
        const raw = (messageData && messageData.folder) ? messageData.folder.toString() : ""
        if (!raw.length)
            return i18n("Folder")
        return (appRoot && appRoot.displayFolderName) ? appRoot.displayFolderName(raw) : raw
    }
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
    required property var systemPalette
    property var tagMap: ({})
    readonly property int threadCount: (messageData && messageData.threadCount) ? messageData.threadCount : 0
    // ── Thread / conversation view ───────────────────────────────────────────
    readonly property string threadId: (messageData && messageData.threadId) ? messageData.threadId.toString() : ""
    readonly property var threadMessages: {
        if (!threadId.length || threadCount < 2 || !appRoot || !appRoot.dataStoreObj)
            return [];
        // Depend on _bodyUpdateVersion so thread members re-appear when their
        // bodies are hydrated.
        void appRoot._bodyUpdateVersion;
        return appRoot.dataStoreObj.messagesForThread(messageData.accountEmail, threadId);
    }
    readonly property string trackerVendor: root._trackerInfo.vendor
    property bool trackingAllowed: false

    function _fileNameFromUrl(url) {
        const raw = (url || "").toString();
        if (!raw.length)
            return i18n("Attachment");
        const noQuery = raw.split("?")[0];
        const parts = noQuery.split("/");
        const last = parts.length ? parts[parts.length - 1] : "";
        return last.length ? decodeURIComponent(last) : i18n("Attachment");
    }
    function textColorForAccent(accent) {
        const c = (accent || "").toString().trim()
        if (c.length !== 7 || c[0] !== "#")
            return "#1E3C5A"
        const r = parseInt(c.slice(1, 3), 16)
        const g = parseInt(c.slice(3, 5), 16)
        const b = parseInt(c.slice(5, 7), 16)
        const yiq = ((r * 299) + (g * 587) + (b * 114)) / 1000
        return yiq >= 150 ? "#1d2433" : "#eef3ff"
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

    function applyTagToSelection(tagObj, targetFolderName) {
        if (!appRoot || !appRoot.imapServiceObj || !appRoot.imapServiceObj.addMessageToFolder)
            return;

        const target = (targetFolderName || tagObj.name || "").toString().trim();
        if (!target.length)
            return;

        const seen = {};
        function applyOne(row) {
            if (!row) return;
            const account = (row.accountEmail || "").toString();
            const folder = (row.folder || "").toString();
            const uid = (row.uid || "").toString();
            if (!account.length || !folder.length || !uid.length) return;
            const k = account + "|" + folder.toLowerCase() + "|" + uid;
            if (seen[k]) return;
            seen[k] = true;
            appRoot.imapServiceObj.addMessageToFolder(account, folder, uid, target);
        }

        if (root.isThreadView && root.threadMessages && root.threadMessages.length > 0) {
            for (let i = 0; i < root.threadMessages.length; ++i)
                applyOne(root.threadMessages[i]);
        } else {
            applyOne(root.messageData);
        }
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
    function forwardAttachmentPathsForCurrentMessage() {
        const refs = [];
        const seen = {};
        if (!root.messageData || !appRoot || !appRoot.imapServiceObj)
            return refs;

        const account = (root.messageData.accountEmail || "").toString();
        const uid = (root.attachmentSourceUid || root.messageData.uid || "").toString();
        const folder = (root.attachmentSourceFolder || root.messageData.folder || "").toString();
        for (let i = 0; i < root.attachmentItems.length; ++i) {
            const a = root.attachmentItems[i] || {};
            const partId = (a.partId || "").toString();
            if (!partId.length)
                continue;

            let p = "";
            if (root.attachmentLocalPaths && root.attachmentLocalPaths[partId])
                p = (root.attachmentLocalPaths[partId] || "").toString();
            if (!p.length && appRoot.imapServiceObj.cachedAttachmentPath)
                p = (appRoot.imapServiceObj.cachedAttachmentPath(account, uid, partId) || "").toString();

            const key = account + "|" + uid + "|" + partId;
            if (seen[key])
                continue;
            seen[key] = true;

            refs.push({
                filename: (a.name || "attachment").toString(),
                path: p,
                accountEmail: account,
                folder: folder,
                uid: uid,
                partId: partId
            });
        }
        return refs;
    }

    function reloadAttachmentsForCurrentMessage() {
        root.attachmentLocalPaths = {};
        root.attachmentSourceUid = "";
        root.attachmentSourceFolder = "";
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
        let activeFolder = folder;

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
                    activeFolder = cf;
                    break;
                }
            }
        }

        root.attachmentItems = items || [];
        root.attachmentSourceUid = activeUid;
        root.attachmentSourceFolder = activeFolder;

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
    function setCurrentTags(tags) {
        const key = appRoot.selectedMessageKey || "";
        if (!key.length)
            return;
        const next = Object.assign({}, tagMap);
        next[key] = tags;
        tagMap = next;
    }

    function rebuildTagsFromDbForCurrentMessage() {
        if (!appRoot || !appRoot.dataStoreObj || !appRoot.dataStoreObj.fetchCandidatesForMessageKey)
            return;

        const account = (root.messageData && root.messageData.accountEmail) ? root.messageData.accountEmail.toString() : "";
        const folder = (root.messageData && root.messageData.folder) ? root.messageData.folder.toString() : "";
        const uid = (root.messageData && root.messageData.uid) ? root.messageData.uid.toString() : "";
        if (!account.length || !folder.length || !uid.length) {
            setCurrentTags([]);
            return;
        }

        const available = (appRoot && appRoot.tagFolderItems) ? appRoot.tagFolderItems() : [];
        const byRaw = {};
        for (let i = 0; i < available.length; ++i) {
            const t = available[i] || {};
            const raw = ((t.rawName || t.name || "").toString()).trim().toLowerCase();
            if (!raw.length) continue;
            byRaw[raw] = t;
        }

        const out = [];
        const seen = {};
        function pushTag(name, accent) {
            const n = (name || "").toString().trim();
            if (!n.length) return;
            const key = n.toLowerCase();
            if (seen[key]) return;
            seen[key] = true;
            const c = (accent || "#D6E8FF").toString();
            out.push({ name: n, color: c, textColor: root.textColorForAccent(c) });
        }

        const candidates = appRoot.dataStoreObj.fetchCandidatesForMessageKey(account, folder, uid) || [];
        for (let i = 0; i < candidates.length; ++i) {
            const c = candidates[i] || {};
            const f = (c.folder || "").toString().trim();
            if (!f.length) continue;
            const lf = f.toLowerCase();

            // Product rule: Important always appears as Important tag if edge exists.
            if (lf === "important" || lf.endsWith("/important")) {
                const imp = byRaw["important"];
                pushTag(imp && imp.name ? imp.name : i18n("Important"), imp && imp.accentColor ? imp.accentColor : "#FFD600");
                continue;
            }

            const t = byRaw[lf];
            if (!t) continue;
            pushTag(t.name || f, t.accentColor || "#D6E8FF");
        }

        setCurrentTags(out);
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
            root.attachmentSourceUid = "";
            root.setCurrentTags([]);
        } else if (!isSameMessage) {
            // Different message — reset state and load fresh.
            root.lastAttachmentMessageKey = messageKey;
            root.attachmentItems = [];
            root.attachmentLocalPaths = ({});
            root.attachmentProgress = ({});
            root.attachmentDownloading = ({});
            root.reloadAttachmentsForCurrentMessage();
            root.startAllAttachmentPrefetchForCurrentMessage();
            root.startImageAttachmentPrefetchForCurrentMessage();
        } else if (root.attachmentItems.length === 0) {
            // Same message, attachments not yet loaded — retry (race with DB hydration).
            root.reloadAttachmentsForCurrentMessage();
            root.startAllAttachmentPrefetchForCurrentMessage();
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

        root.rebuildTagsFromDbForCurrentMessage();

        // Only start a new transition when the message key changes.
        // Background updates (mark-read, header refreshes) must not restart the animation.
        const key = root.renderMessageKey;
        if (key.length && key !== htmlViewComponent.pendingKey)
            htmlViewComponent.startTransition(key);
        // Queue HTML if available (handles case where data arrives before edgeKey).
        htmlViewComponent.onHtmlUpdate();
    }
    onSelectedMessageEdgeKeyChanged: {
        const key = root.selectedMessageEdgeKey;
        // Always reset thread view paging/expanded state on header selection,
        // even when staying within the same thread id.
        threadViewComponent.resetForNewMessage();
        if (key.length && key !== htmlViewComponent.pendingKey)
            htmlViewComponent.startTransition(key);
        // Also try to queue HTML now in case messageData is already consistent.
        htmlViewComponent.onHtmlUpdate();
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

                readonly property var availableTags: (appRoot && appRoot.tagFolderItems) ? appRoot.tagFolderItems() : []

                Instantiator {
                    model: tagMenu.availableTags

                    delegate: QQC2.MenuItem {
                        required property var modelData

                        text: (modelData && modelData.name) ? modelData.name : ""
                        enabled: text.length > 0

                        onTriggered: {
                            const accent = (modelData && modelData.accentColor) ? modelData.accentColor : "#D6E8FF"
                            const tagObj = {
                                name: text,
                                color: accent,
                                textColor: root.textColorForAccent(accent)
                            }
                            root.addTag(tagObj)
                            const targetFolder = (modelData && modelData.rawName) ? modelData.rawName : text
                            root.applyTagToSelection(tagObj, targetFolder)
                        }
                    }

                    onObjectAdded: function(index, object) { tagMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { tagMenu.removeItem(object) }
                }

                QQC2.MenuSeparator {}
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
                    Layout.alignment: Qt.AlignRight
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
                                const subject = i18n("Re: %1", root.messageSubject);
                                appRoot.openComposerTo(root.senderEmail, i18n("reply"), subject);
                            }
                            else if (actionText === i18n("Reply all")) {
                                const subject = i18n("Re: %1", root.messageSubject);
                                appRoot.openComposerTo(root.recipientsText, i18n("reply all"), subject);
                            }
                            else if (actionText === i18n("Forward")) {
                                const forwardAttachments = root.forwardAttachmentPathsForCurrentMessage();
                                appRoot.forwardMessageFromData(root.messageData, root.receivedAtText, forwardAttachments);
                            }
                        }
                    }

                    DarkLightToggleButton {
                        darkMode: root.appRoot.contentPaneDarkModeEnabled

                        onModeToggled: {
                            root.appRoot.contentPaneDarkModeEnabled = !root.appRoot.contentPaneDarkModeEnabled;
                        }
                    }
                }
            }
        }

        MessageInfoBars {
            id: infoBars

            Layout.fillWidth: true
            appRoot: root.appRoot
            hasExternalImages: root.hasExternalImages
            hasTrackingPixel: root.hasTrackingPixel
            imagesAllowed: root.imagesAllowed
            isTrustedSender: root.isTrustedSender
            listUnsubscribeUrl: root.listUnsubscribeUrl
            senderDomain: root.senderDomain
            trackingAllowed: root.trackingAllowed
            trackerVendor: root.trackerVendor
            visible: !root.isThreadView && !!root.messageData && ((root.hasExternalImages && !root.imagesAllowed && !root.isTrustedSender) || (root.hasTrackingPixel && !root.trackingAllowed && root.imagesAllowed) || root.listUnsubscribeUrl.length > 0)

            onAllowTracking: root.trackingAllowed = true
            onLoadImagesAlways: {
                if (root.senderDomain && appRoot && appRoot.dataStoreObj)
                    appRoot.dataStoreObj.setTrustedSenderDomain(root.senderDomain);
                root.imagesAllowed = true;
            }
            onLoadImagesOnce: root.imagesAllowed = true
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
        MessageThreadView {
            id: threadViewComponent

            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.rightMargin: -20
            appRoot: root.appRoot
            forceDarkHtml: root.forceDarkHtml
            messageSubject: root.messageSubject
            systemPalette: root.systemPalette
            threadId: root.threadId
            threadMessages: root.threadMessages
            visible: root.isThreadView
        }

        // ── Single message view + empty placeholder ──────────────────────────
        MessageHtmlView {
            id: htmlViewComponent

            Layout.fillHeight: true
            Layout.fillWidth: true
            appRoot: root.appRoot
            forceDarkHtml: root.forceDarkHtml
            hasUsableBodyHtml: root.hasUsableBodyHtml
            imagesAllowed: root.imagesAllowed
            messageData: root.messageData
            renderMessageKey: root.renderMessageKey
            renderedHtml: root.renderedHtml
            selectedMessageEdgeKey: root.selectedMessageEdgeKey
            visible: !root.isThreadView
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
