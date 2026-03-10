import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami

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
    property var attachmentItems: []
    property var attachmentLocalPaths: ({})
    property var attachmentProgress: ({})
    property var attachmentDownloading: ({})
    property string lastAttachmentMessageKey: ""
    function normalizedEdgeKey(account, folder, uid) {
        const a = (account || "").toString().toLowerCase();
        const f = (folder || "").toString().toLowerCase();
        const u = (uid || "").toString();
        if (!a.length || !u.length)
            return "";
        return a + "|" + f + "|" + u;
    }

    readonly property string renderMessageKey: {
        if (!messageData)
            return "";
        return normalizedEdgeKey(messageData.accountEmail, messageData.folder, messageData.uid);
    }

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

    onSelectedMessageEdgeKeyChanged: {
        htmlContainer.loadHtmlIfChanged("selectedEdgeChanged");
    }
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
        return /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html);
    }

    onHasUsableBodyHtmlChanged: {
        const len = (root.messageBodyHtml || "").toString().length;
        console.log("[hydrate-html-db] pane-usable-changed", "key=", root.renderMessageKey, "usable=", root.hasUsableBodyHtml, "bodyLen=", len)
    }
    property bool imagesAllowed: false
    readonly property bool immersiveBodyMode: !!(appRoot && appRoot.contentPaneHoverExpandActive)
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
    readonly property string renderedHtml: {
        if (!root.hasUsableBodyHtml)
            return "";
        const base = root.htmlForMessage();

        let sanitized = root.sanitizeRenderHtml(base);

        // Neutralize tracking pixels unless the user has explicitly allowed them.
        // Reading root.trackingAllowed here makes this binding reactive to that property.
        if (!root.trackingAllowed)
            sanitized = root.neutralizeTrackingPixels(sanitized);

        // Keep inline local images renderable while external images remain blocked.
        if (!root.imagesAllowed)
            sanitized = root.neutralizeExternalImages(sanitized);

        // Inject a baseline background so plain-text and unstyled messages use the
        // theme background. No !important — emails with their own explicit backgrounds
        // will still override this. Reading the property here keeps the binding reactive.
        const bgColor = Kirigami.Theme.backgroundColor.toString();
        const bgStyle = "<style data-kestrel-bg='baseline'>html,body{background-color:" + bgColor + ";}</style>";
        if (sanitized.indexOf("<head>") >= 0)
            sanitized = sanitized.replace("<head>", "<head>" + bgStyle);

        return root.forceDarkHtml ? root.darkenHtml(sanitized) : sanitized;
    }
    property string selectedAttachmentKey: ""

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
    function darkenHtml(html) {
        const darkBg = Qt.darker(Kirigami.Theme.backgroundColor, 1.35).toString();
        const surfaceBg = Kirigami.Theme.alternateBackgroundColor.toString();
        const lightText = Kirigami.Theme.textColor.toString();
        const borderColor = Kirigami.Theme.disabledTextColor.toString();

        const style = "<style data-dark-mode='baseline'>" + "html, body { background-color:" + darkBg + " !important; color:" + lightText + " !important; }" + "[color] { color:" + lightText + " !important; }" + "td[style*='border'], div[style*='border'], table[style*='border'] { border-color:" + borderColor + " !important; }" + "hr { border-color:" + borderColor + " !important; }" + "img, svg, canvas, video, picture, iframe { filter:none !important; mix-blend-mode:normal !important; opacity:1 !important; }" + "</style>";

        const script = "<script>(function(){" + "var DARK_BG='" + darkBg + "',SURFACE_BG='" + surfaceBg + "',LIGHT_TEXT='" + lightText + "',BORDER_COLOR='" + borderColor + "';" + "var MEDIA={IMG:1,SVG:1,CANVAS:1,VIDEO:1,PICTURE:1,IFRAME:1};" + "function parseRGB(c){if(!c)return null;var m=c.match(/rgba?\\(\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)/);return m?[+m[1],+m[2],+m[3]]:null;}" + "function lum(r,g,b){var a=[r/255,g/255,b/255];for(var i=0;i<3;i++){a[i]=a[i]<=0.03928?a[i]/12.92:Math.pow((a[i]+0.055)/1.055,2.4);}return 0.2126*a[0]+0.7152*a[1]+0.0722*a[2];}" + "function isLight(c){var r=parseRGB(c);return r?lum(r[0],r[1],r[2])>0.35:false;}" + "function isTransparent(c){if(!c)return true;return c==='transparent'||/rgba?\\(.*,\\s*0\\)$/.test(c);}" +
        // isSaturated: R/G/B spread > 20 means a real design color, not a neutral gray.
        // Threshold at 20 (not 30) preserves subtle tints like avatar circle backgrounds
        // (#CAD3E5, spread=27) as well as vivid accent colours (#4CD681, spread=138).
        "function isSaturated(rgb){return Math.max(rgb[0],rgb[1],rgb[2])-Math.min(rgb[0],rgb[1],rgb[2])>20;}" +
        // isImageShell: element contains only media — no real text at any depth.
        // Uses \s+ which matches U+00A0 (&nbsp;), unlike String.trim().
        "function isImageShell(el){if(!el.querySelector||!el.querySelector('img,svg,canvas,video,picture'))return false;return(el.textContent||'').replace(/\\s+/g,'').length===0;}" +
        // isTextLink: <a> that contains no block-level elements is a genuine hyperlink
        // (→ blue); an <a> that wraps a card/section of content is a layout wrapper
        // (→ LIGHT_TEXT, so its children keep their own inline colors).
        "function isTextLink(el){return !el.querySelector('div,table,p,h1,h2,h3,h4,h5,h6,section,article,header,footer');}" +
        // effectiveBg: walk up the DOM to find the nearest non-transparent background.
        // Elements are processed in document order, so parent backgrounds already reflect
        // our darkening changes by the time any child is visited.
        "function effectiveBg(el,doc){var cur=el;while(cur){var b=doc.defaultView.getComputedStyle(cur).backgroundColor;if(!isTransparent(b))return b;cur=cur.parentElement;}return DARK_BG;}" + "function processEl(el,doc){" + "if(MEDIA[el.tagName])return;" + "var cs=doc.defaultView.getComputedStyle(el);if(!cs)return;" + "var bg=cs.backgroundColor,fg=cs.color,hasBgImg=(cs.backgroundImage&&cs.backgroundImage!=='none')||el.hasAttribute('background');" +
        // Background: leave elements with CSS background-image or saturated accent colors alone.
        // Make pure image shells with neutral light bg transparent; darken neutral light bg otherwise.
        "if(!isTransparent(bg)&&!hasBgImg){" + "var rgb=parseRGB(bg);" + "if(rgb&&lum(rgb[0],rgb[1],rgb[2])>0.7&&!isSaturated(rgb)){" + "if(isImageShell(el)){el.style.setProperty('background-color','transparent','important');}" + "else{var tgt=lum(rgb[0],rgb[1],rgb[2])>0.97?DARK_BG:SURFACE_BG;el.style.setProperty('background','none','important');el.style.setProperty('background-color',tgt,'important');}" + "}" + "}" +
        // Text color: only intervene when the actual WCAG contrast ratio between
        // the text and its effective background is below 3:1.  Text that is already
        // readable (including intentional link blues, branded colours, etc.) is left
        // completely untouched.  Text that is genuinely invisible on the dark surface
        // (e.g. black on #2d2d2d) gets lifted to:
        //   • LIGHT_TEXT — for block wrapper <a> tags and neutral-colored text
        //   • #7ab4f5   — for genuine text hyperlinks and saturated-color inline text
        //                 (e.g. <span style="color:#0076B6">See more</span>)
        "var fgR=parseRGB(fg);if(fgR){var fgL=lum(fgR[0],fgR[1],fgR[2]);if(fgL<0.35){var eff=effectiveBg(el,doc);var effR=parseRGB(eff)||[30,30,30];var effL=lum(effR[0],effR[1],effR[2]);var lo=Math.min(fgL,effL),hi=Math.max(fgL,effL);if((hi+0.05)/(lo+0.05)<3){var target=LIGHT_TEXT;if(el.tagName==='A'){target=isTextLink(el)?'#7ab4f5':LIGHT_TEXT;}else if(Math.max(fgR[0],fgR[1],fgR[2])-Math.min(fgR[0],fgR[1],fgR[2])>150){target='#7ab4f5';}el.style.setProperty('color',target,'important');}}}" + "var bc=cs.borderColor;if(bc&&isLight(bc))el.style.setProperty('border-color',BORDER_COLOR,'important');" + "}" + "function run(){try{var d=document;if(!d||!d.body)return;var all=d.querySelectorAll('*');for(var i=0;i<all.length;i++)processEl(all[i],d);}catch(e){console.log('kestrel-dark-error',e);}}" + "if(document.readyState==='complete'||document.readyState==='interactive')run();else document.addEventListener('DOMContentLoaded',run,{once:true});" + "})();</script>";

        const inject = style + script;
        if (html.indexOf("<head>") >= 0) {
            const out = html.replace("<head>", "<head>" + inject);
            return out;
        }
        if (html.indexOf("<html") >= 0) {
            const out = html.replace(/<html[^>]*>/i, function (m) {
                return m + "<head>" + inject + "</head>";
            });
            return out;
        }
        const out = "<html><head>" + inject + "</head><body>" + html + "</body></html>";
        return out;
    }
    function decodeMailtoComponent(s) {
        return decodeURIComponent((s || "").replace(/\+/g, "%20"));
    }
    function decodeQuotedPrintableText(input) {
        let s = (input || "").toString();
        if (!s.length)
            return s;

        // Soft line breaks.
        s = s.replace(/=\r?\n/g, "");

        // Hex escapes (=3D, =0A, ...).
        s = s.replace(/=([0-9A-Fa-f]{2})/g, function (_, hex) {
            const code = parseInt(hex, 16);
            if (isNaN(code))
                return _;
            return String.fromCharCode(code);
        });

        return s;
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

    // If a URL contains a query param whose value is itself a full HTTP URL, return that value.
    // No domain allowlist — any URL with redirect=https://... is a redirect and should be unwrapped.
    function extractTrackingRedirectUrl(href) {
        const m = /[?&](?:redirect|url|to|link|target|dest|goto)=(https?(?:%3A%2F%2F|:\/\/)[^&]*)/i.exec(href);
        if (m) {
            try {
                return decodeURIComponent(m[1]);
            } catch (e) {
                return m[1];
            }
        }
        return null;
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
    function inlineAttachmentBorderColorCss() {
        const c = Kirigami.Theme.backgroundColor;
        const luminance = (0.2126 * c.r) + (0.7152 * c.g) + (0.0722 * c.b);
        if (luminance < 0.5)
            return "rgba(255,255,255,0.42)";
        return Qt.darker(c, 2.0).toString();
    }

    function fileUrlFromLocalPath(localPath) {
        let p = (localPath || "").toString();
        if (!p.length)
            return "";
        p = p.replace(/\\/g, "/");
        // Keep '@' and path semantics intact; only encode characters that require escaping.
        return "file://" + encodeURI(p);
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

    // Replace external image sources with a transparent placeholder while preserving file://,
    // data:, and cid: sources (so inline local images can still render).
    function neutralizeExternalImages(html) {
        const blank = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";
        return html.replace(/<img\b[^>]*>/gi, function (tag) {
            const srcM = tag.match(/\bsrc\s*=\s*(["'])([^"']+)\1/i);
            if (!srcM)
                return tag;
            const src = srcM[2] || "";
            if (/^(file:|data:|cid:)/i.test(src))
                return tag;
            if (!/^https?:/i.test(src))
                return tag;
            return tag.replace(/(\bsrc\s*=\s*)(["'])[^"']+\2/i, '$1"' + blank + '"');
        });
    }

    // Replace the src of any 1×1 external third-party tracking pixel with a transparent data
    // URI so the beacon never fires, even when autoLoadImages is true. Uses the same detection
    // criteria as _trackerInfo (including first-party skip). The original src is preserved in
    // data-tracking-src for later restore.
    function neutralizeTrackingPixels(html) {
        const blank = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";
        const senderDom = root.senderDomain || "";
        let count = 0;
        const result = html.replace(/<img\b[^>]*>/gi, function (tag) {
            if (!/\bsrc\s*=\s*["']https?:/i.test(tag))
                return tag;
            if (!/\bwidth\s*=\s*["']\s*1\s*["']/i.test(tag))
                return tag;
            if (!/\bheight\s*=\s*["']\s*1\s*["']/i.test(tag))
                return tag;
            const srcM = tag.match(/\bsrc\s*=\s*["']([^"']+)/i);
            const src = srcM ? srcM[1] : "(unknown)";
            if (isFirstPartyUrl(src, senderDom)) {
                return tag;
            }
            const vendor = root.trackerVendor || "unknown";
            return tag.replace(/(\bsrc\s*=\s*(["']))([^"']+)\2/i, '$1' + blank + '$2');
        });
        if (count === 0)
        return result;
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
    function removeTagByName(name) {
        const tags = activeTags.filter(function (t) {
            return t.name !== name;
        });
        setCurrentTags(tags);
    }
    function sanitizeRenderHtml(rawHtml) {
        let html = (rawHtml || "").toString();
        if (!html.length)
            return "<html><body></body></html>";

        html = html.replace(/\r\n/g, "\n");

        // Remove binary/control garbage that can break WebEngine parsing.
        html = html.replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F]/g, "");

        // Strip external scripts: prevents blocking DOMContentLoaded (e.g. tracking/analytics
        // scripts that time out) and is correct security hygiene for email HTML.
        html = html.replace(/<script\b[^>]*\bsrc\s*=[^>]*>[\s\S]*?<\/script>/gi, "");

        // Strip external stylesheet links (especially web fonts) to avoid render stalls
        // waiting on third-party CSS/font hosts.
        html = html.replace(/<link\b[^>]*\brel\s*=\s*["']?stylesheet["']?[^>]*>/gi, "");

        // Rewrite known tracking redirect links to their real destination URLs.
        html = sanitizeTrackingLinks(html);

        // Some senders hand us text/plain bodies wrapped as HTML with markdown links
        // like [label](https://...). Convert those into real anchors for rendering.
        html = html.replace(/\[([^\]\n]{1,240})\]\((https?:\/\/[^\s)]+)\)/gi, function (_, label, url) {
            return '<a href="' + url + '">' + label + '</a>';
        });

        // If we already have a full HTML document, avoid destructive rewrites.
        const trimmed = html.trim();
        if (/<html\b/i.test(trimmed) && /<\/html>\s*$/i.test(trimmed)) {
            return trimmed;
        }

        // If we got MIME-ish payload, cut to content after first header break.
        const mimeHeaderLike = /^(mime-version:|content-type:|content-transfer-encoding:|x-[a-z0-9-]+:)/im.test(html);
        if (mimeHeaderLike) {
            const splitAt = html.search(/\n\n/);
            if (splitAt >= 0 && splitAt < html.length - 2) {
                html = html.slice(splitAt + 2);
            }
        }

        // Drop obvious IMAP protocol lines that occasionally leak into payload.
        html = html.split("\n").filter(function (line) {
            const t = line.trim();
            if (!t.length)
                return true;
            if (/^\*\s+\d+\s+fetch\s*\(/i.test(t))
                return false;
            if (/^[a-z]\d+\s+(ok|no|bad)\b/i.test(t))
                return false;
            if (/^body\[header\.fields/i.test(t))
                return false;
            return true;
        }).join("\n");

        // Decode quoted-printable artifacts when present.
        if (/=\r?\n|=[0-9A-Fa-f]{2}/.test(html)) {
            html = decodeQuotedPrintableText(html);
        }

        // Strip MIME preamble lines that sometimes leak into body content.
        const lines = html.split("\n");
        const kept = [];
        let stripping = true;
        for (let i = 0; i < lines.length; ++i) {
            const t = lines[i].trim();
            const headerLike = /^content-|^mime-version:|^x-/i.test(t);
            const boundaryLike = /^--[_=A-Za-z0-9.:-]+$/.test(t);
            if (stripping && (headerLike || boundaryLike || t === "")) {
                if (t === "" && i > 0)
                    stripping = false;
                continue;
            }
            stripping = false;
            kept.push(lines[i]);
        }
        html = kept.join("\n").trim();

        // Ensure we always hand WebEngine a full document.
        const hasHtmlTag = /<html\b/i.test(html);
        const hasBodyTag = /<body\b/i.test(html);
        const hasHtmlish = /<html|<body|<div|<table|<p|<br|<span|<img|<a\b/i.test(html);

        if (!hasHtmlish) {
            const escaped = html.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
            return "<!doctype html><html><head><meta charset='utf-8'></head><body><pre style='white-space:pre-wrap;font-family:sans-serif;'>" + escaped + "</pre></body></html>";
        }

        if (hasHtmlTag) {
            if (!/<head\b/i.test(html)) {
                html = html.replace(/<html[^>]*>/i, function (m) {
                    return m + "<head><meta charset='utf-8'></head>";
                });
            } else if (!/<meta\s+charset=/i.test(html)) {
                html = html.replace(/<head[^>]*>/i, function (m) {
                    return m + "<meta charset='utf-8'>";
                });
            }
            if (!hasBodyTag) {
                html = html.replace(/<\/html>\s*$/i, "<body></body></html>");
            }
            return html;
        }

        if (hasBodyTag) {
            return "<!doctype html><html><head><meta charset='utf-8'></head>" + html + "</html>";
        }

        return "<!doctype html><html><head><meta charset='utf-8'></head><body>" + html + "</body></html>";
    }

    // Replace known tracking redirect hrefs with their final destination URLs.
    function sanitizeTrackingLinks(html) {
        return html.replace(/(<a\b[^>]*\bhref\s*=\s*)(["'])(https?:\/\/[^"']{20,})\2/gi, function (match, pre, quote, href) {
            const dest = extractTrackingRedirectUrl(href);
            if (dest) {
                return pre + quote + dest + quote;
            }
            return match;
        });
    }
    function setCurrentTags(tags) {
        const key = appRoot.selectedMessageKey || "";
        if (!key.length)
            return;
        const next = Object.assign({}, tagMap);
        next[key] = tags;
        tagMap = next;
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

    onMessageDataChanged: {
        root.selectedAttachmentKey = "";
        htmlContainer.pendingHtml = "";
        htmlContainer.pendingMessageKey = "";
        htmlContainer.pendingLoadReason = "";
        htmlContainer.pendingLoadQueuedAtMs = 0;
        htmlContainer.pendingLoadStartedAtMs = 0;

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

        // Force a load attempt on explicit selection changes; relying only on
        // renderedHtml binding changes can miss transitions when data races.
        htmlContainer.loadHtmlIfChanged("messageDataChanged");
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
            visible: !!root.messageData

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
            visible: !!root.messageData && ((root.hasExternalImages && !root.imagesAllowed && !root.isTrustedSender) || (root.hasTrackingPixel && !root.trackingAllowed && root.imagesAllowed) || root.listUnsubscribeUrl.length > 0)

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

                    required property var modelData
                    readonly property bool selected: root.selectedAttachmentKey === attachmentCard.modelData.partId
                    readonly property string sizeText: root.formatAttachmentSize(attachmentCard.modelData.bytes)
                    readonly property string localPath: (root.attachmentLocalPaths && root.attachmentLocalPaths[attachmentCard.modelData.partId]) ? root.attachmentLocalPaths[attachmentCard.modelData.partId] : ""
                    readonly property string previewSource: {
                        if (!localPath.length || !root.messageData || !appRoot || !appRoot.imapServiceObj)
                            return "";
                        return appRoot.imapServiceObj.attachmentPreviewPath(root.messageData.accountEmail,
                                                                            root.messageData.uid,
                                                                            attachmentCard.modelData.partId,
                                                                            attachmentCard.modelData.name,
                                                                            attachmentCard.modelData.mimeType || "");
                    }

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
                        onHoveredChanged: if (hovered) root.startAllAttachmentPrefetchForCurrentMessage()
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
                        targetHovered: attachmentHover.hovered || attachmentCard.selected
                        previewSource: attachmentCard.previewSource
                        previewMimeType: (attachmentCard.modelData.mimeType || "")
                        fallbackIcon: root.iconForAttachment(attachmentCard.modelData.mimeType, attachmentCard.modelData.name)
                        downloadProgress: Number(root.attachmentProgress[attachmentCard.modelData.partId] || 0)
                        downloadComplete: Number(root.attachmentProgress[attachmentCard.modelData.partId] || 0) >= 100
                        arrowLeftPx: Math.max(24, attachmentCard.width * 0.25)
                        openButtonText: i18n("Open")
                        saveButtonText: i18n("Save")
                        onOpenTriggered: appRoot.imapServiceObj.openAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding)
                        onSaveTriggered: appRoot.imapServiceObj.saveAttachment(root.messageData.accountEmail, root.messageData.folder, root.messageData.uid, attachmentCard.modelData.partId, attachmentCard.modelData.name, attachmentCard.modelData.encoding)
                    }
                }
            }
        }
        Rectangle {
            id: htmlContainer

            property real bodyOpacity: 1.0
            property real flickVelocityY: 0
            property string lastLoadedHtmlKey: ""
            property string pendingHtml: ""
            property string pendingLoadReason: ""
            property string pendingMessageKey: ""
            property string activeLoadMessageKey: ""
            property double pendingLoadQueuedAtMs: 0
            property double pendingLoadStartedAtMs: 0
            property double pendingClickAtMs: 0
            property double pendingLoadCompletedAtMs: 0
            property string pendingCompletedReason: ""
            property bool suppressNextLoadCommit: false

            function loadHtmlIfChanged(reason) {
                const t0 = Date.now();
                if (!root.renderedHtml.length) {
                    return;
                }
                if (root.selectedMessageEdgeKey.length > 0 && root.renderMessageKey !== root.selectedMessageEdgeKey) {
                    return;
                }
                const key = root.renderMessageKey + "|" + (root.imagesAllowed ? "1" : "0") + "|" + root.renderedHtml;
                if (key === lastLoadedHtmlKey) {
                    return;
                }
                lastLoadedHtmlKey = key;
                pendingHtml = root.renderedHtml;
                pendingLoadReason = reason;
                pendingMessageKey = root.renderMessageKey;
                pendingLoadQueuedAtMs = t0;
                pendingClickAtMs = (root.appRoot && root.appRoot.lastMessageClickAtMs) ? root.appRoot.lastMessageClickAtMs : 0;
                const clickToQueue = pendingClickAtMs > 0 ? (pendingLoadQueuedAtMs - pendingClickAtMs) : -1;
                fadeOutLoadTimer.restart();
            }
            function scrollHtmlBy(deltaY) {
                if (!root.hasUsableBodyHtml)
                    return;
                const amount = Number(deltaY);
                if (!isFinite(amount) || Math.abs(amount) < 0.01)
                    return;
                htmlView.runJavaScript("window.scrollBy(0," + Math.round(amount) + ");");
            }

            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "transparent"
            visible: !!root.messageData

            Behavior on bodyOpacity {
                NumberAnimation {
                    duration: 250
                    easing.type: Easing.InOutQuad
                }
            }

            Timer {
                id: fadeOutLoadTimer

                interval: 0
                repeat: false

                onTriggered: {
                    if (!htmlContainer.pendingHtml.length)
                        return;
                    if (htmlContainer.pendingMessageKey.length > 0 && htmlContainer.pendingMessageKey !== root.renderMessageKey) {
                        htmlContainer.pendingHtml = "";
                        htmlContainer.pendingMessageKey = "";
                        return;
                    }
                    htmlContainer.bodyOpacity = 0.0;
                    loadAfterFadeTimer.restart();
                }
            }

            Timer {
                id: loadAfterFadeTimer

                interval: 250
                repeat: false

                onTriggered: {
                    if (!htmlContainer.pendingHtml.length)
                        return;
                    if (htmlContainer.pendingMessageKey.length > 0 && htmlContainer.pendingMessageKey !== root.renderMessageKey) {
                        htmlContainer.pendingHtml = "";
                        htmlContainer.pendingMessageKey = "";
                        return;
                    }
                    // Drop previous document right at fade-out completion before loading next.
                    htmlView.loadHtml("<!doctype html><html><body></body></html>", "file:///");
                    htmlContainer.pendingLoadStartedAtMs = Date.now();
                    htmlContainer.activeLoadMessageKey = htmlContainer.pendingMessageKey;
                    htmlView.loadHtml(htmlContainer.pendingHtml, "file:///");
                }
            }

            Timer {
                id: visiblePerfTimer
                interval: 0
                repeat: false
                onTriggered: {
                    const now = Date.now();
                    const loadToVisible = htmlContainer.pendingLoadCompletedAtMs > 0 ? (now - htmlContainer.pendingLoadCompletedAtMs) : -1;
                    const clickToVisible = htmlContainer.pendingClickAtMs > 0 ? (now - htmlContainer.pendingClickAtMs) : -1;
                    htmlContainer.pendingLoadCompletedAtMs = 0;
                    htmlContainer.pendingClickAtMs = 0;
                    htmlContainer.pendingCompletedReason = "";
                }
            }
            WebEngineView {
                id: htmlView

                anchors.fill: parent
                opacity: htmlContainer.bodyOpacity
                settings.autoLoadImages: true
                settings.errorPageEnabled: true
                settings.localContentCanAccessRemoteUrls: true
                settings.localContentCanAccessFileUrls: true
                visible: root.hasUsableBodyHtml

                Component.onCompleted: htmlContainer.loadHtmlIfChanged("component")
                onLoadingChanged: function (req) {
                    const st = req.status;
                    if (st === WebEngineLoadingInfo.LoadSucceededStatus || st === WebEngineLoadingInfo.LoadFailedStatus) {
                        const loadedForCurrent = !htmlContainer.activeLoadMessageKey.length
                                              || htmlContainer.activeLoadMessageKey === root.selectedMessageEdgeKey;
                        const hasNewerPending = htmlContainer.pendingMessageKey.length > 0
                                              && htmlContainer.pendingMessageKey !== htmlContainer.activeLoadMessageKey;

                        if (!loadedForCurrent || hasNewerPending) {
                            // Ignore visual commit for stale completion or when a newer message is already queued.
                            htmlContainer.activeLoadMessageKey = "";
                            htmlContainer.pendingLoadReason = "";
                            htmlContainer.pendingLoadQueuedAtMs = 0;
                            htmlContainer.pendingLoadStartedAtMs = 0;
                            return;
                        }

                        const tDone = Date.now();
                        const loadMs = htmlContainer.pendingLoadStartedAtMs > 0 ? (tDone - htmlContainer.pendingLoadStartedAtMs) : -1;
                        const totalMs = htmlContainer.pendingLoadQueuedAtMs > 0 ? (tDone - htmlContainer.pendingLoadQueuedAtMs) : -1;
                        const clickToLoad = htmlContainer.pendingClickAtMs > 0 ? (tDone - htmlContainer.pendingClickAtMs) : -1;

                        htmlContainer.pendingLoadCompletedAtMs = tDone;
                        htmlContainer.pendingCompletedReason = htmlContainer.pendingLoadReason;
                        htmlContainer.pendingHtml = "";
                        htmlContainer.pendingMessageKey = "";
                        htmlContainer.activeLoadMessageKey = "";
                        htmlContainer.pendingLoadReason = "";
                        htmlContainer.pendingLoadQueuedAtMs = 0;
                        htmlContainer.pendingLoadStartedAtMs = 0;
                        htmlContainer.bodyOpacity = 1.0;
                        visiblePerfTimer.restart();
                    }
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
                        htmlContainer.loadHtmlIfChanged("imagesAllowed");
                    }
                    function onRenderedHtmlChanged() {
                        htmlContainer.loadHtmlIfChanged("renderedHtml");
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
        target: appRoot ? appRoot.imapServiceObj : null

        function onAttachmentReady(accountEmail, uid, partId, localPath) {

            if (!root.messageData) return;
            if (accountEmail !== root.messageData.accountEmail || uid !== root.messageData.uid) return;
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

        function onAttachmentDownloadProgress(accountEmail, uid, partId, progressPercent) {
            // console.log("[attachment-progress] signal",
            //             "account=", accountEmail,
            //             "uid=", uid,
            //             "partId=", partId,
            //             "progress=", progressPercent)

            if (!root.messageData) return;
            if (accountEmail !== root.messageData.accountEmail || uid !== root.messageData.uid) return;
            const prev = Number(root.attachmentProgress[partId] || 0);
            const next = (progressPercent === 0 && prev > 0 && prev < 100) ? prev : Math.max(prev, progressPercent);

            const p = Object.assign({}, root.attachmentProgress);
            p[partId] = next;
            root.attachmentProgress = p;

            const d = Object.assign({}, root.attachmentDownloading);
            d[partId] = next < 100;
            root.attachmentDownloading = d;
        }
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
