import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Qt5Compat.GraphicalEffects
import "../Common"

Rectangle {
    id: messageCard

    property var appRoot
    property var groupedMessageList // Pointer to the ListView

    readonly property bool isChecked: !!(appRoot && appRoot.selectedMessageKeys && appRoot.selectedMessageKeys[messageKeyValue])

    // Necessary for layout and logic
    property bool isHeaderRow
    readonly property string mailboxForAvatar: {
        if (showRecipient)
            return modelRecipient || "";
        // When thread-dedup picks the user's own reply as the latest message,
        // the sender is self — fall back to recipient for avatar lookup.
        const sEmail = appRoot ? appRoot.senderEmail(modelSender || "") : "";
        const acct = (modelAccountEmail || "").toString().trim().toLowerCase();
        if (sEmail.length && acct.length && sEmail === acct)
            return modelRecipient || "";
        return modelSender || "";
    }
    readonly property string messageKeyValue: modelMessageKey || ""
    property var modelAccountEmail
    property var modelFolder
    property bool modelHasAttachments
    property bool modelHasTrackingPixel
    property int modelIndex
    property bool modelIsImportant

    // Model properties passed in from delegate
    property var modelMessageKey
    property var modelReceivedAt
    property var modelRecipient
    property var modelSender
    property var modelSnippet
    property var modelSubject
    property int modelThreadCount
    property var modelUid
    property bool modelUnread
    property var modelAllSenders
    property bool modelFlagged
    property bool modelIsSearchResult: false
    property string modelResultFolder: ""
    readonly property string nameLabel: {
        if (!appRoot)
            return "";
        if (showRecipient)
            return i18n("To: %1", appRoot.displayRecipientNames(modelRecipient, modelAccountEmail));
        if (modelThreadCount > 1 && modelAllSenders) {
            const participants = appRoot.displayThreadParticipants(modelAllSenders, modelAccountEmail);
            if (participants.length)
                return participants;
        }
        return appRoot.displaySenderName(modelSender, modelAccountEmail);
    }
    readonly property string selectedFolderKey: (appRoot && appRoot.selectedFolderKey) ? appRoot.selectedFolderKey.toString().toLowerCase() : ""
    readonly property string selectedFolderNorm: (appRoot && appRoot.normalizedFolderFromKey) ? appRoot.normalizedFolderFromKey(appRoot.selectedFolderKey).toString().toLowerCase() : "" || ""
    readonly property bool showRecipient: selectedFolderNorm === "sent" || selectedFolderNorm === "draft" || selectedFolderNorm === "drafts" || selectedFolderNorm.indexOf("/sent") >= 0 || selectedFolderNorm.indexOf("/sent ") >= 0 || selectedFolderNorm.indexOf("/draft") >= 0
    property var systemPalette
    property int tagsEpoch: 0
    readonly property var snippetInfo: snippetTagItems()

    function textColorForAccent(accent) {
        const c = (accent || "").toString().trim();
        if (c.length !== 7 || c[0] !== "#")
            return KestrelColors.tagDarkText;
        const r = parseInt(c.slice(1, 3), 16);
        const g = parseInt(c.slice(3, 5), 16);
        const b = parseInt(c.slice(5, 7), 16);
        const yiq = ((r * 299) + (g * 587) + (b * 114)) / 1000;
        return yiq >= 150 ? KestrelColors.tagDarkOnBright : KestrelColors.tagLightOnDark;
    }

    function snippetTagItems() {
        void tagsEpoch;
        if (!appRoot || !appRoot.dataStoreObj || !appRoot.dataStoreObj.fetchCandidatesForMessageKey)
            return { tags: [], isImportant: false };
        const account = (modelAccountEmail || "").toString();
        const folder = (modelFolder || "").toString();
        const uid = (modelUid || "").toString();
        if (!account.length || !folder.length || !uid.length)
            return { tags: [], isImportant: false };

        const available = (appRoot.tagFolderItems) ? appRoot.tagFolderItems() : [];
        const byRaw = {};
        for (let i = 0; i < available.length; ++i) {
            const t = available[i] || {};
            const raw = ((t.rawName || t.name || "").toString()).trim().toLowerCase();
            if (!raw.length)
                continue;
            byRaw[raw] = t;
        }

        const folderLower = folder.trim().toLowerCase();
        const candidates = appRoot.dataStoreObj.fetchCandidatesForMessageKey(account, folder, uid) || [];
        const out = [];
        const seen = {};
        let liveImportant = false;

        for (let i = 0; i < candidates.length; ++i) {
            const c = candidates[i] || {};
            const f = (c.folder || "").toString().trim();
            if (!f.length)
                continue;

            const lf = f.toLowerCase();
            if (lf === folderLower)
                continue;
            if (lf === "important" || lf.endsWith("/important")) {
                liveImportant = true;
                continue;
            }

            const t = byRaw[lf];
            if (!t)
                continue;

            const name = (t.name || f || "").toString().trim();
            if (!name.length)
                continue;
            const key = name.toLowerCase();
            if (seen[key])
                continue;
            seen[key] = true;

            const accent = (t.accentColor || KestrelColors.tagDefaultAccent).toString();
            out.push({
                name: name,
                color: accent,
                textColor: textColorForAccent(accent)
            });
        }

        return { tags: out, isImportant: liveImportant };
    }

    border.color: "transparent"
    clip: true
    color: _isDragPlaceholder ? "transparent" : isChecked ? (systemPalette ? Qt.lighter(systemPalette.highlight, 1.35) : "transparent") : (appRoot && appRoot.selectedMessageKey === messageKeyValue) ? (systemPalette ? Qt.lighter(systemPalette.highlight, 1.18) : "transparent") : "transparent"
    height: Kirigami.Units.gridUnit * 3 + Kirigami.Units.smallSpacing + 14
    radius: 0
    visible: !isHeaderRow
    width: parent.width

    Rectangle {
        visible: messageCard._isDragPlaceholder
        anchors.fill: parent
        anchors.leftMargin: 7
        anchors.rightMargin: 5
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        color: "transparent"
        border.width: 1
        border.color: Kirigami.Theme.highlightColor
    }

    HoverHandler {
        id: rowHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    Connections {
        target: appRoot && appRoot.dataStoreObj ? appRoot.dataStoreObj : null
        function onDataChanged() {
            messageCard.tagsEpoch = messageCard.tagsEpoch + 1
        }
    }

    property bool _isDragPlaceholder: false
    property bool _dragStarted: false
    property real _pressX: 0
    property real _pressY: 0
    property int _pressModifiers: 0
    property Item _dragVisual: null
    property point _dragVisualStartPos: Qt.point(0, 0)
    property point _dragOffset: Qt.point(0, 0)

    function _beginDrag() {
        if (!appRoot) { return }
        var keys = Object.keys(appRoot.selectedMessageKeys)
        var dragKeys = {}
        if (keys.length > 0 && appRoot.selectedMessageKeys[messageKeyValue]) {
            dragKeys = Object.assign({}, appRoot.selectedMessageKeys)
        } else {
            dragKeys[messageKeyValue] = true
        }
        appRoot.messageDragKeys = dragKeys
        appRoot.messageDragCount = Object.keys(dragKeys).length
        appRoot.messageDragActive = true

        // Grab a pixel-perfect screenshot of the card, then create the drag visual
        var overlay = appRoot.messageDragProxy ? appRoot.messageDragProxy.parent : null
        if (!overlay) { return }

        messageCard.grabToImage(function(result) {
            if (!result) { return }

            _dragVisual = Qt.createQmlObject(
                'import QtQuick; import org.kde.kirigami as Kirigami;
                 Rectangle {
                     property real slideOffset: 0
                     Behavior on slideOffset { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                     scale: 0.92; opacity: 0.88; transformOrigin: Item.TopLeft
                     radius: 4
                     color: Kirigami.Theme.backgroundColor
                     border.width: 1; border.color: Kirigami.Theme.highlightColor
                     Image { id: img; anchors.fill: parent; anchors.margins: 1; source: "" }
                 }',
                overlay
            )
            _dragVisual.children[0].source = result.url
            _dragVisual.width = messageCard.width
            _dragVisual.height = messageCard.height
            _dragVisual.z = 100000

            var cardPos = messageCard.mapToItem(overlay, 0, 0)
            _dragVisual.x = cardPos.x
            _dragVisual.y = cardPos.y
            _dragVisualStartPos = Qt.point(_dragVisual.x, _dragVisual.y)
            _dragOffset = Qt.point(messageCard._pressX, messageCard._pressY)

            // Hide content, show placeholder border
            messageCard._isDragPlaceholder = true
        })

        // Show the invisible Drag carrier so DropArea works
        if (appRoot.messageDragProxy) { appRoot.messageDragProxy.visible = true }
    }

    function _finishDrag(dropped) {
        if (!_dragVisual) {
            messageCard._isDragPlaceholder = false
            appRoot.cancelMessageDrag()
            return
        }

        var visual = _dragVisual
        var card = messageCard

        if (dropped) {
            // Animate toward the cursor (drop point) while scaling down — "sucked into folder"
            var dropX = appRoot.messageDragProxy ? appRoot.messageDragProxy.x : visual.x
            var dropY = appRoot.messageDragProxy ? appRoot.messageDragProxy.y : visual.y
            // Target: visual center converges on the drop point
            var targetX = dropX - (visual.width * 0.5)
            var targetY = dropY - (visual.height * 0.5)

            visual.transformOrigin = Item.Center
            var xAnim = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 250; easing.type: Easing.InCubic }', visual)
            var yAnim = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 250; easing.type: Easing.InCubic }', visual)
            var scaleAnim = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 250; easing.type: Easing.InCubic }', visual)
            var opacityAnim = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 250; easing.type: Easing.InCubic }', visual)

            xAnim.target = visual; xAnim.property = "x"; xAnim.to = targetX
            yAnim.target = visual; yAnim.property = "y"; yAnim.to = targetY
            scaleAnim.target = visual; scaleAnim.property = "scale"; scaleAnim.to = 0.05
            opacityAnim.target = visual; opacityAnim.property = "opacity"; opacityAnim.to = 0

            var completed = 0
            function onDropDone() {
                completed++
                if (completed === 4) {
                    visual.destroy()
                    card._dragVisual = null
                    card._isDragPlaceholder = false
                    xAnim.destroy(); yAnim.destroy(); scaleAnim.destroy(); opacityAnim.destroy()
                }
            }

            xAnim.finished.connect(onDropDone)
            yAnim.finished.connect(onDropDone)
            scaleAnim.finished.connect(onDropDone)
            opacityAnim.finished.connect(onDropDone)
            xAnim.start(); yAnim.start(); scaleAnim.start(); opacityAnim.start()
            appRoot.cancelMessageDrag()
            return
        }

        // Snap-back animation
        var startX = _dragVisualStartPos.x
        var startY = _dragVisualStartPos.y

        var xAnim = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 200; easing.type: Easing.OutCubic }', visual)
        var yAnim = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 200; easing.type: Easing.OutCubic }', visual)
        var scaleAnim2 = Qt.createQmlObject('import QtQuick; NumberAnimation { duration: 200; easing.type: Easing.OutCubic }', visual)

        xAnim.target = visual; xAnim.property = "x"; xAnim.to = startX
        yAnim.target = visual; yAnim.property = "y"; yAnim.to = startY
        scaleAnim2.target = visual; scaleAnim2.property = "scale"; scaleAnim2.to = 1.0

        var completed2 = 0
        function onSnapDone() {
            completed2++
            if (completed2 === 3) {
                visual.destroy()
                card._dragVisual = null
                card._isDragPlaceholder = false
                xAnim.destroy(); yAnim.destroy(); scaleAnim2.destroy()
            }
        }

        xAnim.finished.connect(onSnapDone)
        yAnim.finished.connect(onSnapDone)
        scaleAnim2.finished.connect(onSnapDone)
        xAnim.start(); yAnim.start(); scaleAnim2.start()
        appRoot.cancelMessageDrag()
    }

    MouseArea {
        id: messageMouseArea

        anchors.fill: parent
        hoverEnabled: true

        onPressed: function(mouse) {
            messageCard._dragStarted = false
            messageCard._pressX = mouse.x
            messageCard._pressY = mouse.y
            messageCard._pressModifiers = mouse.modifiers
        }

        onPositionChanged: function(mouse) {
            if (!(mouse.buttons & Qt.LeftButton)) { return }
            if (messageCard._dragStarted) {
                if (messageCard._dragVisual && appRoot && appRoot.messageDragProxy) {
                    const overlay = appRoot.messageDragProxy.parent;
                    const mapped = messageCard.mapToItem(overlay, mouse.x, mouse.y);
                    const normalX = mapped.x - messageCard._dragOffset.x;
                    // slideOffset animates smoothly; x is set directly each frame
                    // Slide visual out of the way when cursor enters the folder pane
                    let overFolderPane = false;
                    if (appRoot.folderPane) {
                        const fp = appRoot.folderPane.mapToItem(overlay, 0, 0);
                        overFolderPane = mapped.x >= fp.x && mapped.x < fp.x + appRoot.folderPane.width
                    }
                    messageCard._dragVisual.slideOffset = overFolderPane
                        ? (mapped.x + 15) - normalX : 0
                    const visualX = normalX + messageCard._dragVisual.slideOffset;
                    const visualY = mapped.y - messageCard._dragOffset.y;
                    const vw = messageCard._dragVisual.width * messageCard._dragVisual.scale;
                    const vh = messageCard._dragVisual.height * messageCard._dragVisual.scale;
                    messageCard._dragVisual.x = Math.max(0, Math.min(visualX, overlay.width - vw))
                    messageCard._dragVisual.y = Math.max(0, Math.min(visualY, overlay.height - vh))
                    // Keep the invisible Drag carrier at the cursor for DropArea hit-testing
                    appRoot.messageDragProxy.x = mapped.x
                    appRoot.messageDragProxy.y = mapped.y
                }
                return
            }
            const dx = mouse.x - messageCard._pressX;
            const dy = mouse.y - messageCard._pressY;
            const absDx = Math.abs(dx);
            const absDy = Math.abs(dy);
            // Start drag once horizontal movement exceeds 5px. Vertical
            // movement only passes through for flicking if horizontal stays
            // under 10px.
            if (absDx > 5) {
                messageCard._dragStarted = true
                preventStealing = true
                messageCard._beginDrag()
            }
        }

        onReleased: function(mouse) {
            preventStealing = false
            if (messageCard._dragStarted) {
                messageCard._dragStarted = false
                if (appRoot && appRoot.messageDragActive) {
                    var proxy = appRoot.messageDragProxy
                    var dropResult = proxy ? proxy.Drag.drop() : Qt.IgnoreAction
                    var dropped = (dropResult !== Qt.IgnoreAction)
                    messageCard._finishDrag(dropped)
                }
                return
            }
            // Normal click
            if (!appRoot) { return }
            var mod = messageCard._pressModifiers
            if (mod & Qt.ShiftModifier && appRoot.lastClickedMessageIndex >= 0 && appRoot.messageListModelObj) {
                var from = Math.min(modelIndex, appRoot.lastClickedMessageIndex)
                var to = Math.max(modelIndex, appRoot.lastClickedMessageIndex)
                var next = Object.assign({}, appRoot.selectedMessageKeys)
                var mdl = appRoot.messageListModelObj
                for (var i = from; i <= to; ++i) {
                    var row = mdl.rowAt(i)
                    if (row && !row.isHeader && row.messageKey) { next[row.messageKey] = true }
                }
                appRoot.selectedMessageKeys = next
            } else if (mod & Qt.ControlModifier) {
                appRoot.lastClickedMessageIndex = modelIndex
                var next2 = Object.assign({}, appRoot.selectedMessageKeys)
                if (next2[messageCard.messageKeyValue]) { delete next2[messageCard.messageKeyValue] }
                else { next2[messageCard.messageKeyValue] = true }
                appRoot.selectedMessageKeys = next2
            } else {
                appRoot.lastClickedMessageIndex = modelIndex
                appRoot.lastMessageClickAtMs = Date.now()
                appRoot.selectedMessageKeys = ({})
                appRoot.selectedMessageKey = messageCard.messageKeyValue
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing
        visible: !messageCard._isDragPlaceholder

        MessageCardStatusColumn {
            // Unread dot
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                color: systemPalette ? systemPalette.highlight : "transparent"
                height: 9
                radius: 4.5
                visible: !!modelUnread
                width: 9
            }

            // Flag icon — always visible when flagged, shown on hover otherwise
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: 20
                Layout.preferredWidth: 20
                visible: modelFlagged || rowHover.hovered

                MouseArea {
                    id: flagMouseArea
                    anchors.fill: parent
                    hoverEnabled: true

                    onClicked: function (mouse) {
                        mouse.accepted = true;
                        if (!messageCard.appRoot) return;
                        messageCard.appRoot.toggleMessageFlagged(
                            (modelAccountEmail || "").toString(),
                            (modelFolder || "").toString(),
                            (modelUid || "").toString(),
                            !!modelFlagged);
                    }
                }

                Image {
                    id: flagImage
                    anchors.centerIn: parent
                    fillMode: Image.PreserveAspectFit
                    layer.enabled: true
                    source: "qrc:/qml/images/flag.svg"
                    sourceSize: Qt.size(20, 20)
                }

                ColorOverlay {
                    anchors.fill: flagImage
                    color: modelFlagged ? "#E53935"
                                       : (flagMouseArea.containsMouse ? Kirigami.Theme.textColor
                                                                       : Qt.darker(Kirigami.Theme.textColor, 1.25))
                    source: flagImage
                }
            }
        }

        MessageCardAvatar {
            accountEmail: modelAccountEmail
            appRoot: messageCard.appRoot
            displayName: appRoot ? (messageCard.showRecipient ? appRoot.displayRecipientNames(modelRecipient, modelAccountEmail) : appRoot.displaySenderName(modelSender, modelAccountEmail)) : ""
            fallbackText: messageCard.mailboxForAvatar
            mailbox: messageCard.mailboxForAvatar
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    text: messageCard.nameLabel
                    wrapMode: Text.NoWrap
                }

                QQC2.Label {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    opacity: 0.7
                    text: (appRoot) ? appRoot.formatListDate(modelReceivedAt) : ""
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                QQC2.Label {
                    Layout.fillWidth: true
                    color: Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    font.bold: !!modelUnread
                    font.pixelSize: 13
                    opacity: 0.85
                    text: (modelSubject || i18n("(No subject)"))
                    wrapMode: Text.NoWrap
                }

                // Search result: folder badge
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitHeight: 18
                    implicitWidth: Math.min(140, folderBadgeText.implicitWidth + 16)
                    radius: 3
                    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                    border.width: 2
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 2.2)
                    visible: !!messageCard.modelIsSearchResult && messageCard.modelResultFolder.length > 0

                    QQC2.Label {
                        id: folderBadgeText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignHCenter
                        color: Kirigami.Theme.textColor
                        elide: Text.ElideRight
                        font.pixelSize: 10
                        font.bold: true
                        maximumLineCount: 1
                        text: (messageCard.appRoot && messageCard.appRoot.displayFolderName)
                              ? messageCard.appRoot.displayFolderName(messageCard.modelResultFolder)
                              : messageCard.modelResultFolder
                        wrapMode: Text.NoWrap
                    }
                }

                // Search result: account icon
                Image {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignVCenter
                    fillMode: Image.PreserveAspectFit
                    source: "qrc:/qml/images/gmail_account_icon.svg"
                    sourceSize: Qt.size(16, 16)
                    visible: !!messageCard.modelIsSearchResult
                }

                MessageCardIndicators {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    hasAttachments: !!modelHasAttachments
                    hasTrackingPixel: !!modelHasTrackingPixel
                    threadCount: modelThreadCount || 0
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Image {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: 20
                    Layout.preferredWidth: 20
                    fillMode: Image.PreserveAspectFit
                    source: "qrc:/qml/images/important.svg"
                    sourceSize: Qt.size(20, 20)
                    visible: !!snippetInfo.isImportant
                }

                Repeater {
                    model: messageCard.snippetInfo.tags
                    delegate: Rectangle {
                        required property var modelData
                        Layout.alignment: Qt.AlignVCenter
                        implicitHeight: 18
                        implicitWidth: Math.min(140, tagText.implicitWidth + 12)
                        radius: Math.round(implicitHeight / 2)
                        color: (modelData && modelData.color) ? modelData.color : KestrelColors.tagDefaultAccent
                        opacity: 0.92

                        QQC2.Label {
                            id: tagText
                            anchors.centerIn: parent
                            color: (modelData && modelData.textColor) ? modelData.textColor : messageCard.textColorForAccent((modelData && modelData.color) ? modelData.color : KestrelColors.tagDefaultAccent)
                            elide: Text.ElideRight
                            font.pixelSize: 10
                            maximumLineCount: 1
                            text: (modelData && modelData.name) ? modelData.name : ""
                            wrapMode: Text.NoWrap
                            width: parent.width - 10
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    color: Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    opacity: 0.72
                    text: modelSnippet || ""
                    visible: text.length > 0 || !!snippetInfo.isImportant
                    wrapMode: Text.NoWrap
                }
            }
        }

        MessageCardStatusColumn {
            // Trash icon on hover
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: 20
                Layout.preferredWidth: 20
                visible: rowHover.hovered

                Image {
                    id: trashImage
                    anchors.centerIn: parent
                    fillMode: Image.PreserveAspectFit
                    layer.enabled: true
                    source: "qrc:/qml/images/trash.svg"
                    sourceSize: Qt.size(20, 20)
                }

                ColorOverlay {
                    anchors.fill: trashImage
                    color: trashMouseArea.containsMouse ? Kirigami.Theme.textColor : Qt.darker(Kirigami.Theme.textColor, 1.25)
                    source: trashImage
                }

                MouseArea {
                    id: trashMouseArea

                    anchors.fill: parent
                    enabled: rowHover.hovered
                    hoverEnabled: true

                    onClicked: function (mouse) {
                        mouse.accepted = true;
                        if (!messageCard.appRoot) return;
                        // If nothing is multi-selected, select this card so
                        // deleteSelectedMessages() targets it specifically.
                        if (Object.keys(messageCard.appRoot.selectedMessageKeys).length === 0)
                            messageCard.appRoot.selectedMessageKeys = ({ [messageCard.messageKeyValue]: true });
                        messageCard.appRoot.deleteSelectedMessages();
                    }
                }
            }
        }
    }
}
