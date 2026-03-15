import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import ".."
import "../Attachments"

Item {
    id: root

    required property var appRoot
    required property bool forceDarkHtml
    required property string messageSubject
    required property var systemPalette
    required property string threadId
    required property var threadMessages

    // ── Internal thread state ─────────────────────────────────────────────
    property var cardAttachmentItems: ({})
    property var cardAttachmentLocalPaths: ({})
    property var cardAttachmentProgress: ({})
    property var cardSelectedKey: ({})
    property var cardDarkModes: ({})
    property bool threadLoadingOlder: false
    property int threadScrollEpoch: 0
    property var threadExpandedSet: null
    property bool threadShowAll: false
    property int threadVisibleCount: 5

    readonly property int threadHiddenCount: Math.max(0, threadMessages.length - visibleThreadMessages.length)
    readonly property int threadOffTopCount: threadHiddenCount + threadScrolledOffTopCount
    readonly property int threadScrolledOffTopCount: {
        void threadScrollEpoch;
        return _threadScrolledOffTopCount();
    }
    readonly property var visibleThreadMessages: {
        const msgs = threadMessages;
        if (threadShowAll || msgs.length <= threadVisibleCount)
            return msgs;
        return msgs.slice(msgs.length - threadVisibleCount);
    }

    // Reset paging and expand state — call on every header selection change.
    function resetForNewMessage() {
        threadShowAll = false;
        threadVisibleCount = 5;
        threadLoadingOlder = false;
        threadScrollEpoch = 0;
        threadExpandedSet = null;
        cardDarkModes = ({});
        cardAttachmentItems = ({});
        cardAttachmentLocalPaths = ({});
        cardAttachmentProgress = ({});
        cardSelectedKey = ({});
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

    function _threadAvatarSources(msg) {
        if (!msg || !msg.sender)
            return [];
        return appRoot.senderAvatarSources(msg.sender, msg.avatarDomain || "", msg.avatarUrl || "", msg.accountEmail || "");
    }
    function _threadBodyTextForCard(index) {
        void cardDarkModes;
        void cardAttachmentLocalPaths;
        const msgs = visibleThreadMessages;
        if (index < 0 || index >= msgs.length)
            return "";
        const msg = msgs[index];
        if (!msg.bodyHtml)
            return "";
        const baseHtml = msg.bodyHtml.toString();
        const cardKey = _threadMessageKey(msg);
        const inlineImages = _cardInlineImageHtml(cardKey);
        let html = baseHtml;
        if (inlineImages.length) {
            if (/<\/body\s*>/i.test(html))
                html = html.replace(/<\/body\s*>/i, inlineImages + "</body>");
            else
                html = html + inlineImages;
        }
        return renderedHtmlForThread(html, _threadCardDarkMode(index));
    }
    function _threadCardDarkMode(index) {
        return cardDarkModes[index] !== undefined ? cardDarkModes[index] : forceDarkHtml;
    }
    function _threadClampScrollToLastCardTop() {
        const count = visibleThreadMessages.length;
        if (count <= 0)
            return;
        if (_threadIsExpanded(count - 1))
            return;
        const lastItem = threadCardsRepeater.itemAt(count - 1);
        if (!lastItem)
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
    function _threadMaxContentY() {
        const count = visibleThreadMessages.length;
        if (count <= 0)
            return 0;
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
        const a = (msg.accountEmail || "").toString().toLowerCase();
        const f = (msg.folder || "").toString().toLowerCase();
        const u = (msg.uid || "").toString();
        if (!a.length || !u.length)
            return "";
        return a + "|" + f + "|" + u;
    }
    function _threadOnCardExpanded(index) {
        const msgs = visibleThreadMessages;
        if (index < 0 || index >= msgs.length)
            return;
        _threadHydrateIfNeeded(msgs[index]);
        _loadCardAttachments(msgs[index]);
    }
    function _threadRecipientName(msg) {
        if (!msg || !msg.recipient)
            return "";
        return appRoot.displayRecipientNames(msg.recipient, msg.accountEmail || "");
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
    function _threadToggleCardDark(index) {
        const next = Object.assign({}, cardDarkModes);
        next[index] = !_threadCardDarkMode(index);
        cardDarkModes = next;
    }
    function _threadToggleExpand(index) {
        if (threadExpandedSet === null) {
            const last = visibleThreadMessages.length - 1;
            const initial = {};
            initial[last] = true;
            threadExpandedSet = initial;
        }
        const next = Object.assign({}, threadExpandedSet);
        const collapsing = !!next[index];
        if (collapsing) {
            delete next[index];
            // Clear attachment selection when collapsing the card.
            const msgs = visibleThreadMessages;
            if (index >= 0 && index < msgs.length) {
                const key = _threadMessageKey(msgs[index]);
                if (key.length && cardSelectedKey[key]) {
                    const sk = Object.assign({}, cardSelectedKey);
                    delete sk[key];
                    cardSelectedKey = sk;
                }
            }
        } else {
            next[index] = true;
        }
        threadExpandedSet = next;
        Qt.callLater(function () {
            threadScrollEpoch++;
        });
    }

    function _cardInlineImageHtml(cardKey) {
        const items = cardAttachmentItems[cardKey] || [];
        const paths = cardAttachmentLocalPaths[cardKey] || {};
        if (!items.length)
            return "";
        const images = [];
        for (let i = 0; i < items.length; i++) {
            const a = items[i] || {};
            const mt = (a.mimeType || "").toString().toLowerCase();
            const name = (a.name || "").toString();
            const isImage = mt.startsWith("image/") || /\.(png|jpe?g|webp|gif|bmp|svg)$/i.test(name);
            if (!isImage)
                continue;
            const partId = (a.partId || "").toString();
            const localPath = partId.length ? (paths[partId] || "") : "";
            if (!localPath.length)
                continue;
            const src = fileUrlFromLocalPath(localPath);
            const alt = name.replace(/"/g, "&quot;");
            images.push("<img src=\"" + src + "\" alt=\"" + alt + "\" style=\"display:block;max-width:100%;height:auto;margin:8px auto 0 auto;\" />");
        }
        if (!images.length)
            return "";
        return "<div data-kestrel-inline-attachments='1' style='margin-top:12px;padding-top:8px;border-top:1px solid " + inlineAttachmentBorderColorCss() + ";'>" + images.join("") + "</div>";
    }
    function fileUrlFromLocalPath(localPath) {
        let p = (localPath || "").toString();
        if (!p.length)
            return "";
        p = p.replace(/\\/g, "/");
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
    function _loadCardAttachments(msg) {
        if (!msg || !appRoot || !appRoot.imapServiceObj)
            return;
        const key = _threadMessageKey(msg);
        if (!key.length)
            return;
        if (cardAttachmentItems[key] !== undefined)
            return;
        const account = (msg.accountEmail || "").toString();
        const folder = (msg.folder || "").toString();
        const uid = (msg.uid || "").toString();
        if (!account.length || !folder.length || !uid.length)
            return;
        let items = appRoot.imapServiceObj.attachmentsForMessage(account, folder, uid);
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
                    break;
                }
            }
        }
        items = items || [];
        const paths = {};
        for (let i = 0; i < items.length; i++) {
            const a = items[i];
            const lp = appRoot.imapServiceObj.cachedAttachmentPath(account, uid, a.partId);
            if (lp.length > 0)
                paths[a.partId] = lp;
        }
        const nextItems = Object.assign({}, cardAttachmentItems);
        nextItems[key] = items;
        cardAttachmentItems = nextItems;
        if (Object.keys(paths).length > 0) {
            const nextPaths = Object.assign({}, cardAttachmentLocalPaths);
            nextPaths[key] = paths;
            cardAttachmentLocalPaths = nextPaths;
        }
    }

    function _startCardAttachmentPrefetch(msg) {
        if (!msg || !appRoot || !appRoot.imapServiceObj)
            return;
        const account = (msg.accountEmail || "").toString();
        const folder = (msg.folder || "").toString();
        const uid = (msg.uid || "").toString();
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

    onThreadIdChanged: {
        threadShowAll = false;
        threadVisibleCount = 5;
        threadLoadingOlder = false;
        threadScrollEpoch = 0;
        threadExpandedSet = null;
        cardDarkModes = ({});
        cardAttachmentItems = ({});
        cardAttachmentLocalPaths = ({});
        cardAttachmentProgress = ({});
        cardSelectedKey = ({});
    }
    onVisibleThreadMessagesChanged: {
        Qt.callLater(function () {
            if (!root.threadLoadingOlder)
                _threadClampScrollToLastCardTop();
            threadScrollEpoch++;
        });
    }

    Connections {
        target: root.appRoot ? root.appRoot.imapServiceObj : null

        function onAttachmentDownloadProgress(accountEmail, uid, partId, progressPercent) {
            const msgs = root.visibleThreadMessages;
            for (let i = 0; i < msgs.length; i++) {
                const msg = msgs[i];
                if (!msg)
                    continue;
                if ((msg.uid || "").toString() !== uid.toString())
                    continue;
                if ((msg.accountEmail || "").toString().toLowerCase() !== accountEmail.toString().toLowerCase())
                    continue;
                const key = root._threadMessageKey(msg);
                if (!key.length)
                    continue;
                if (!root.cardAttachmentItems[key])
                    continue;
                const nextProgress = Object.assign({}, root.cardAttachmentProgress);
                const existing = Object.assign({}, nextProgress[key] || {});
                const prev = Number(existing[partId] || 0);
                existing[partId] = (progressPercent === 0 && prev > 0 && prev < 100) ? prev : Math.max(prev, progressPercent);
                nextProgress[key] = existing;
                root.cardAttachmentProgress = nextProgress;
                break;
            }
        }

        function onAttachmentReady(accountEmail, uid, partId, localPath) {
            const msgs = root.visibleThreadMessages;
            for (let i = 0; i < msgs.length; i++) {
                const msg = msgs[i];
                if (!msg)
                    continue;
                if ((msg.uid || "").toString() !== uid.toString())
                    continue;
                if ((msg.accountEmail || "").toString().toLowerCase() !== accountEmail.toString().toLowerCase())
                    continue;
                const key = root._threadMessageKey(msg);
                if (!key.length)
                    continue;
                if (!root.cardAttachmentItems[key])
                    continue;
                const nextPaths = Object.assign({}, root.cardAttachmentLocalPaths);
                const existingPaths = Object.assign({}, nextPaths[key] || {});
                existingPaths[partId] = localPath;
                nextPaths[key] = existingPaths;
                root.cardAttachmentLocalPaths = nextPaths;

                const nextProgress = Object.assign({}, root.cardAttachmentProgress);
                const existingProgress = Object.assign({}, nextProgress[key] || {});
                existingProgress[partId] = 100;
                nextProgress[key] = existingProgress;
                root.cardAttachmentProgress = nextProgress;
                break;
            }
        }
    }

    // ── Layout: "show older" button + thread flickable ────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.Button {
            id: showOlderBtn

            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 4
            bottomPadding: 6
            flat: true
            leftPadding: 16
            rightPadding: 16
            text: i18n("Show %1 older message(s)", root.threadOffTopCount)
            topPadding: 6
            visible: root.threadOffTopCount > 0

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
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.rightMargin: -20

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

                    Repeater {
                        id: threadCardsRepeater

                        model: root.visibleThreadMessages

                        delegate: Rectangle {
                            id: threadCard

                            property real attachFlowH: (isExpanded && cardAttachFlow._items.length > 0) ? (cardAttachFlow.implicitHeight + 8) : 0
                            property real bodyHeight: 24
                            readonly property string cardBodyHtml: root._threadBodyTextForCard(index)
                            required property int index
                            readonly property bool isExpanded: root._threadIsExpanded(index)
                            required property var modelData

                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
                            border.width: 1
                            color: isExpanded ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35) : Kirigami.Theme.backgroundColor
                            height: implicitHeight
                            implicitHeight: cardHeaderRow.implicitHeight + 32 + (isExpanded ? attachFlowH + bodyHeight + 12 : 0)
                            radius: 8
                            width: threadScrollContent.width

                            onIsExpandedChanged: {
                                if (isExpanded)
                                    root._threadOnCardExpanded(index);
                            }

                            // ── Card header ────────────────────────────────────────
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

                                Item {
                                    Layout.alignment: Qt.AlignTop
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

                                ColumnLayout {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.fillWidth: true
                                    spacing: 2

                                    QQC2.Label {
                                        id: threadSenderLabel
                                        Layout.fillWidth: false
                                        color: "#4ea3ff"
                                        elide: Text.ElideRight
                                        font.bold: true
                                        font.pixelSize: 13
                                        text: root._threadSenderName(threadCard.modelData)

                                        MouseArea {
                                            id: threadSenderMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            onClicked: appRoot.openComposerTo(appRoot.senderEmail(threadCard.modelData.sender || ""), i18n("sender"))
                                        }
                                        ContactHoverPopup {
                                            anchorItem: threadSenderLabel
                                            avatarSources: root._threadAvatarSources(threadCard.modelData)
                                            avatarText: appRoot.avatarInitials(root._threadSenderName(threadCard.modelData))
                                            primaryButtonText: i18n("Send mail")
                                            secondaryButtonText: i18n("Add to contacts")
                                            subtitleText: appRoot.senderEmail(threadCard.modelData.sender || "")
                                            targetHovered: threadSenderMouse.containsMouse
                                            titleText: root._threadSenderName(threadCard.modelData)
                                            onPrimaryTriggered: appRoot.openComposerTo(appRoot.senderEmail(threadCard.modelData.sender || ""), i18n("tooltip send"))
                                            onSecondaryTriggered: appRoot.showInlineStatus(i18n("Add to contacts requested for %1").arg(appRoot.senderEmail(threadCard.modelData.sender || "")), false)
                                        }
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
                                            id: threadRecipientLabel
                                            Layout.fillWidth: false
                                            color: "#4ea3ff"
                                            elide: Text.ElideRight
                                            font.bold: true
                                            font.pixelSize: 12
                                            text: root._threadRecipientName(threadCard.modelData)

                                            MouseArea {
                                                id: threadRecipientMouse
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                hoverEnabled: true
                                                onClicked: appRoot.openComposerTo(threadCard.modelData.recipient || "", i18n("recipient"))
                                            }
                                            ContactHoverPopup {
                                                anchorItem: threadRecipientLabel
                                                avatarSources: appRoot.senderAvatarSources(threadCard.modelData.recipient || "", "", "", threadCard.modelData.accountEmail || "")
                                                avatarText: (root._threadRecipientName(threadCard.modelData) || "?").slice(0, 1).toUpperCase()
                                                primaryButtonText: i18n("Send mail")
                                                secondaryButtonText: i18n("Add to contacts")
                                                subtitleText: threadCard.modelData.recipient || ""
                                                targetHovered: threadRecipientMouse.containsMouse
                                                titleText: root._threadRecipientName(threadCard.modelData)
                                                onPrimaryTriggered: appRoot.openComposerTo(threadCard.modelData.recipient || "", i18n("recipient"))
                                                onSecondaryTriggered: appRoot.showInlineStatus(i18n("Add to contacts requested for %1").arg(threadCard.modelData.recipient || ""), false)
                                            }
                                        }
                                    }
                                }

                                // Date + action buttons
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.fillWidth: false
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

                            // ── Attachment bar ─────────────────────────────────────
                            Flow {
                                id: cardAttachFlow

                                readonly property var _items: root.cardAttachmentItems[root._threadMessageKey(threadCard.modelData)] || []
                                readonly property var _paths: root.cardAttachmentLocalPaths[root._threadMessageKey(threadCard.modelData)] || ({})
                                readonly property var _progress: root.cardAttachmentProgress[root._threadMessageKey(threadCard.modelData)] || ({})
                                readonly property string _selectedKey: root.cardSelectedKey[root._threadMessageKey(threadCard.modelData)] || ""

                                spacing: Kirigami.Units.smallSpacing
                                visible: threadCard.isExpanded && _items.length > 0
                                width: threadCard.width - 20
                                x: 10
                                y: cardHeaderRow.implicitHeight + 16

                                Repeater {
                                    model: cardAttachFlow._items

                                    delegate: AttachmentCard {
                                        id: cardAttachItem

                                        required property var modelData
                                        property string localPath: cardAttachFlow._paths[modelData.partId] || ""
                                        property string previewSource: {
                                            if (!localPath.length || !appRoot || !appRoot.imapServiceObj)
                                                return "";
                                            return appRoot.imapServiceObj.attachmentPreviewPath(
                                                threadCard.modelData.accountEmail, threadCard.modelData.uid,
                                                modelData.partId, modelData.name, modelData.mimeType || "");
                                        }

                                        attachmentName:     modelData.name
                                        attachmentMimeType: modelData.mimeType || ""
                                        attachmentBytes:    modelData.bytes || 0
                                        selected:           cardAttachFlow._selectedKey === modelData.partId
                                        showRemoveButton:   false

                                        onChipEntered: root._startCardAttachmentPrefetch(threadCard.modelData)
                                        onChipClicked: function (mouse) {
                                            root._startCardAttachmentPrefetch(threadCard.modelData);
                                            const cardKey = root._threadMessageKey(threadCard.modelData);
                                            const next = Object.assign({}, root.cardSelectedKey);
                                            if (mouse.button === Qt.LeftButton) {
                                                next[cardKey] = (next[cardKey] === cardAttachItem.modelData.partId) ? "" : cardAttachItem.modelData.partId;
                                            } else if (mouse.button === Qt.RightButton) {
                                                next[cardKey] = cardAttachItem.modelData.partId;
                                                cardAttachMenu.popup();
                                            }
                                            root.cardSelectedKey = next;
                                        }
                                        onChipDoubleClicked: function (mouse) {
                                            if (mouse.button !== Qt.LeftButton) return;
                                            root._startCardAttachmentPrefetch(threadCard.modelData);
                                            appRoot.imapServiceObj.openAttachment(
                                                threadCard.modelData.accountEmail, threadCard.modelData.folder,
                                                threadCard.modelData.uid, cardAttachItem.modelData.partId,
                                                cardAttachItem.modelData.name, cardAttachItem.modelData.encoding);
                                        }

                                        QQC2.Menu {
                                            id: cardAttachMenu

                                            QQC2.MenuItem {
                                                text: i18n("Open Attachment")
                                                onTriggered: appRoot.imapServiceObj.openAttachment(
                                                    threadCard.modelData.accountEmail, threadCard.modelData.folder,
                                                    threadCard.modelData.uid, cardAttachItem.modelData.partId,
                                                    cardAttachItem.modelData.name, cardAttachItem.modelData.encoding)
                                            }
                                            QQC2.MenuItem {
                                                text: i18n("Save as…")
                                                onTriggered: appRoot.imapServiceObj.saveAttachment(
                                                    threadCard.modelData.accountEmail, threadCard.modelData.folder,
                                                    threadCard.modelData.uid, cardAttachItem.modelData.partId,
                                                    cardAttachItem.modelData.name, cardAttachItem.modelData.encoding)
                                            }
                                        }
                                        AttachmentHoverPopup {
                                            anchorItem:       cardAttachItem
                                            arrowLeftPx:      Math.max(24, cardAttachItem.width * 0.25)
                                            downloadComplete: Number(cardAttachFlow._progress[cardAttachItem.modelData.partId] || 0) >= 100
                                            downloadProgress: Number(cardAttachFlow._progress[cardAttachItem.modelData.partId] || 0)
                                            fallbackIcon:     cardAttachItem._icon
                                            openButtonText:   i18n("Open")
                                            previewMimeType:  cardAttachItem.modelData.mimeType || ""
                                            previewSource:    cardAttachItem.previewSource
                                            saveButtonText:   i18n("Save")
                                            targetHovered:    cardAttachItem.hovered

                                            onOpenTriggered: appRoot.imapServiceObj.openAttachment(
                                                threadCard.modelData.accountEmail, threadCard.modelData.folder,
                                                threadCard.modelData.uid, cardAttachItem.modelData.partId,
                                                cardAttachItem.modelData.name, cardAttachItem.modelData.encoding)
                                            onSaveTriggered: appRoot.imapServiceObj.saveAttachment(
                                                threadCard.modelData.accountEmail, threadCard.modelData.folder,
                                                threadCard.modelData.uid, cardAttachItem.modelData.partId,
                                                cardAttachItem.modelData.name, cardAttachItem.modelData.encoding)
                                        }
                                    }
                                }
                            }

                            // ── Expanded body ──────────────────────────────────────
                            Loader {
                                id: bodyLoader

                                active: threadCard.isExpanded
                                height: threadCard.bodyHeight
                                width: threadCard.width - 20
                                x: 10
                                y: cardHeaderRow.implicitHeight + 16 + threadCard.attachFlowH

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
                                            settings.localContentCanAccessFileUrls: true
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

                                        // Hard wheel-capture layer so the thread pane scroll works
                                        // even when the cursor is over WebEngine content.
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

                            // Watch for body HTML becoming available after hydration.
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
    }
}
