import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var appRoot
    required property bool forceDarkHtml
    required property bool hasUsableBodyHtml
    required property bool imagesAllowed
    required property var messageData
    required property string renderMessageKey
    required property string renderedHtml
    required property string selectedMessageEdgeKey

    // Key of the message currently being transitioned to (read-only for callers).
    readonly property string pendingKey: htmlContainer.pendingKey

    // Called from parent when renderedHtml or imagesAllowed changes.
    function onHtmlUpdate() {
        htmlContainer.onHtmlUpdate();
    }

    // Begin an animated transition to a new message key.
    function startTransition(key) {
        htmlContainer.startTransition(key);
    }

    function decodeMailtoComponent(s) {
        return decodeURIComponent((s || "").replace(/\+/g, "%20"));
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

    // ── Single-message HTML view ──────────────────────────────────────────
    Rectangle {
        id: htmlContainer

        // ── Fade & load state ─────────────────────────────────────────────
        property real bodyOpacity: 1.0
        property bool fadedOut: false
        property real flickVelocityY: 0
        property string lastHtmlKey: ""
        property string loadingKey: ""
        property string pendingHtml: ""
        property string pendingKey: ""

        // Hand the queued HTML to the WebEngineView.
        function doLoad() {
            if (!pendingHtml.length || pendingKey !== root.selectedMessageEdgeKey)
                return;
            loadingKey = pendingKey;
            htmlView.loadHtml(pendingHtml, "file:///");
        }

        // Called whenever renderedHtml or imagesAllowed changes.
        function onHtmlUpdate() {
            const key = root.renderMessageKey;
            const html = root.renderedHtml;
            if (!html.length || !key.length)
                return;
            // Allow HTML through if it matches the pending transition key, even when
            // selectedMessageEdgeKey is transiently wrong (inbox refresh / binding race).
            // pendingKey was just set by startTransition and is the authoritative intent.
            if (root.selectedMessageEdgeKey.length > 0
                    && key !== root.selectedMessageEdgeKey
                    && key !== pendingKey)
                return;

            const dedup = key + "|" + (root.imagesAllowed ? "1" : "0") + "|" + html;
            if (dedup === lastHtmlKey)
                return;

            if (key !== pendingKey)
                startTransition(key);

            lastHtmlKey = dedup;
            pendingHtml = html;
            if (fadedOut || !fadeTimer.running)
                doLoad();
        }

        // Begin an animated transition to a new message key.
        function startTransition(key) {
            pendingKey = key;
            pendingHtml = "";
            lastHtmlKey = "";
            fadedOut = false;
            loadingKey = "";
            bodyOpacity = 0.0;
            fadeTimer.restart();
        }

        anchors.fill: parent
        color: "transparent"
        visible: !!root.messageData

        Behavior on bodyOpacity {
            NumberAnimation {
                duration: 250
                easing.type: Easing.InOutQuad
            }
        }

        Timer {
            id: fadeTimer

            interval: 250
            repeat: false

            onTriggered: {
                if (htmlContainer.pendingKey !== root.selectedMessageEdgeKey)
                    return;
                htmlContainer.fadedOut = true;
                if (htmlContainer.pendingHtml.length) {
                    htmlContainer.doLoad();
                } else {
                    // pendingHtml is empty — either HTML isn't available yet, or it was
                    // previously blocked by a transient edgeKey mismatch. Re-run onHtmlUpdate
                    // now that fadedOut=true and edgeKey should be stable.
                    htmlContainer.onHtmlUpdate();
                }
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
                    if (!root.handleMailtoUrl(url))
                        Qt.openUrlExternally(url);
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

    // ── Empty placeholder ─────────────────────────────────────────────────
    Item {
        anchors.fill: parent
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
