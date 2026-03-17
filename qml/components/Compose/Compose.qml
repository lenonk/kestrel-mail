import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import ".."
import "../Attachments"

Window {
    id: root

    readonly property color _bg: Kirigami.Theme.backgroundColor
    readonly property color _bg2: Qt.lighter(_bg, 1.08)
    readonly property color _border: Kirigami.Theme.disabledTextColor
    readonly property int _btnH: Kirigami.Units.gridUnit + 5
    readonly property int _btnW: Kirigami.Units.gridUnit + 16
    readonly property int _icoSz: Kirigami.Units.gridUnit - 4
    readonly property int _lblW: 72
    readonly property int _rowH: 36
    readonly property int _titleH: Kirigami.Units.gridUnit + 12

    // ── Public API ─────────────────────────────────────────────────────────
    property var accountRepositoryObj: null
    property var dataStoreObj: null

    // Format toggle states
    property bool fmtBold: false
    property bool fmtItalic: false
    property bool fmtStrike: false
    property bool fmtUnderline: false
    property string sendError: ""
    property bool sending: false
    property bool showCcBcc: false
    property var smtpServiceObj: null

    signal sendRequested

    // ── Helpers ────────────────────────────────────────────────────────────
    function _addChipToModel(model, raw) {
        const text = raw.trim();
        if (!text.length)
            return;
        let display = text, email = text;
        const lt = text.lastIndexOf('<');
        const gt = text.lastIndexOf('>');
        if (lt >= 0 && gt > lt) {
            email = text.slice(lt + 1, gt).trim();
            display = text.slice(0, lt).trim().replace(/^"|"$/g, '');
            if (!display.length)
                display = email;
        }
        model.append({
            display: display,
            email: email
        });
    }
    function _applyFormat(open, close) {
        const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
        if (s === e) {
            bodyArea.insert(bodyArea.cursorPosition, open + close);
            bodyArea.cursorPosition -= close.length;
        } else {
            const sel = bodyArea.selectedText;
            bodyArea.remove(s, e);
            bodyArea.insert(s, open + sel + close);
        }
        bodyArea.forceActiveFocus();
    }
    function _applyLinePrefix(linePrefix) {
        const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
        const sel = s === e ? "" : bodyArea.selectedText;
        if (!sel.length) {
            bodyArea.insert(bodyArea.cursorPosition, linePrefix);
        } else {
            const lines = sel.split("\n");
            bodyArea.remove(s, e);
            bodyArea.insert(s, lines.map(l => linePrefix + l).join("\n"));
        }
        bodyArea.forceActiveFocus();
    }
    function _chipListToStrings(model) {
        const arr = [];
        for (let i = 0; i < model.count; i++)
            arr.push('"' + model.get(i).display + '" <' + model.get(i).email + '>');
        return arr;
    }
    function _accountDisplayText(account) {
        if (!account)
            return i18n("Select account");
        const name = (account.displayName || account.fullName || account.senderName || "").toString().trim();
        const email = (account.email || "").toString().trim();
        if (name.length && email.length)
            return '"' + name + '" <' + email + '>';
        return email.length ? email : (name.length ? name : i18n("Select account"));
    }
    function _doSend() {
        if (!smtpServiceObj) {
            sendError = i18n("SMTP service not available");
            return;
        }
        toRow.commitInput();
        ccRow.commitInput();
        bccRow.commitInput();
        if (toChipModel.count === 0) {
            sendError = i18n("Please add at least one recipient");
            return;
        }
        sending = true;
        sendError = "";
        const fromEmail = accountCombo.currentIndex >= 0 && accountRepositoryObj ? accountRepositoryObj.accounts[accountCombo.currentIndex].email : "";
        const attachPaths = [];
        for (let i = 0; i < attachmentModel.count; i++)
            attachPaths.push(attachmentModel.get(i).path);
        smtpServiceObj.sendEmail({
            fromEmail: fromEmail,
            toList: _chipListToStrings(toChipModel),
            ccList: _chipListToStrings(ccChipModel),
            bccList: _chipListToStrings(bccChipModel),
            subject: subjectField.text,
            body: bodyArea.text,
            attachments: attachPaths
        });
    }
    function openCompose(to, subject, body) {
        toChipModel.clear();
        ccChipModel.clear();
        bccChipModel.clear();
        subjectField.text = subject || "";
        bodyArea.text = body || "";
        showCcBcc = false;
        sendError = "";
        sending = false;
        fmtBold = false;
        fmtItalic = false;
        fmtUnderline = false;
        fmtStrike = false;
        attachmentModel.clear();
        if (to && to.trim().length > 0)
            _addChipToModel(toChipModel, to.trim());
        root.show();
        root.raise();
        root.requestActivate();
        if (toChipModel.count === 0)
            toRow.focusInput();
        else
            subjectField.forceActiveFocus();
    }
    function openComposeReply(params) {
        toChipModel.clear();
        ccChipModel.clear();
        bccChipModel.clear();
        subjectField.text = params.subject || "";
        bodyArea.text = params.body || "";
        showCcBcc = !!(params.ccList && params.ccList.length > 0);
        sendError = "";
        sending = false;
        fmtBold = false;
        fmtItalic = false;
        fmtUnderline = false;
        fmtStrike = false;
        attachmentModel.clear();
        const toList = params.toList || [];
        for (let i = 0; i < toList.length; i++)
            _addChipToModel(toChipModel, toList[i]);
        if (params.ccList) {
            for (let i = 0; i < params.ccList.length; i++)
                _addChipToModel(ccChipModel, params.ccList[i]);
        }
        root.show();
        root.raise();
        root.requestActivate();
        if (toChipModel.count === 0)
            toRow.focusInput();
        else
            subjectField.forceActiveFocus();
    }

    color: Kirigami.Theme.backgroundColor
    flags: Qt.Window | Qt.FramelessWindowHint
    height: 660
    minimumHeight: 420
    minimumWidth: 560
    title: subjectField.text.trim().length > 0 ? subjectField.text.trim() + " – " + i18n("New Message") : i18n("[no subject] – New Message")
    visible: false
    width: 840

    // ── Internal state ─────────────────────────────────────────────────────
    SystemPalette {
        id: sysPalette

    }
    ListModel {
        id: toChipModel

    }
    ListModel {
        id: ccChipModel

    }
    ListModel {
        id: bccChipModel

    }
    ListModel {
        id: attachmentModel

    }
    Connections {
        function onSendFinished(ok, message) {
            sending = false;
            if (ok) {
                root.hide();
                root.sendRequested();
            } else {
                sendError = message;
            }
        }

        target: root.smtpServiceObj
    }
    FileDialog {
        id: attachDialog

        fileMode: FileDialog.OpenFiles
        title: i18n("Attach File")

        onAccepted: {
            for (let i = 0; i < selectedFiles.length; i++) {
                const url = selectedFiles[i].toString();
                const path = decodeURIComponent(url.replace(/^file:\/\//, ""));
                const filename = path.split("/").pop();
                attachmentModel.append({
                    filename: filename,
                    path: path
                });
            }
        }
    }

    // ── Root chrome ─────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root._bg

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ── Title bar ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root._titleH
                color: root._bg2

                MouseArea {
                    acceptedButtons: Qt.LeftButton

                    onPressed: root.startSystemMove()

                    anchors {
                        fill: parent
                        leftMargin: root._btnW + 4
                        rightMargin: root._btnW * 3 + 4
                    }
                }
                TitleBarIconButton {
                    buttonHeight: root._btnH
                    buttonWidth: root._btnW
                    highlightColor: sysPalette.highlight
                    iconName: "open-menu-symbolic"
                    iconSize: root._icoSz

                    onClicked: composeMenu.openIfClosed()

                    anchors {
                        left: parent.left
                        leftMargin: Kirigami.Units.smallSpacing
                        verticalCenter: parent.verticalCenter
                    }
                }
                QQC2.Label {
                    anchors.centerIn: parent
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                    font.bold: false
                    font.pixelSize: 12
                    text: root.title
                    width: Math.min(implicitWidth, parent.width - root._btnW * 5)
                }
                Row {
                    spacing: 2

                    anchors {
                        right: parent.right
                        rightMargin: Kirigami.Units.smallSpacing
                        verticalCenter: parent.verticalCenter
                    }
                    TitleBarIconButton {
                        buttonHeight: root._btnH
                        buttonWidth: root._btnW
                        highlightColor: sysPalette.highlight
                        iconName: "window-minimize-symbolic"
                        iconSize: root._icoSz

                        onClicked: root.showMinimized()
                    }
                    TitleBarIconButton {
                        buttonHeight: root._btnH
                        buttonWidth: root._btnW
                        highlightColor: sysPalette.highlight
                        iconName: root.visibility === Window.Maximized ? "window-restore-symbolic" : "window-maximize-symbolic"
                        iconSize: root._icoSz

                        onClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                    }
                    TitleBarIconButton {
                        buttonHeight: root._btnH
                        buttonWidth: root._btnW
                        highlightColor: sysPalette.highlight
                        iconName: "window-close-symbolic"
                        iconSize: root._icoSz

                        onClicked: root.hide()
                    }
                }

                PopupMenu {
                    id: composeMenu

                    parent: root.contentItem
                    x: Kirigami.Units.smallSpacing
                    y: root._titleH

                    QQC2.MenuItem {
                        text: i18n("Save Draft")
                    }
                    QQC2.MenuItem {
                        text: i18n("Discard")
                    }
                    QQC2.MenuSeparator {
                    }
                    QQC2.MenuItem {
                        text: i18n("Settings")
                    }
                }
            }

            // ── Action bar: Send + From ────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root._titleH + 6
                Layout.leftMargin: 12
                color: root._bg2

                RowLayout {
                    Layout.alignment: Qt.AlignVCenter

                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter

                    MailActionButton {
                        id: sendBtn

                        alwaysHighlighted: true
                        enabled: true
                        iconName: root.sending ? "view-refresh" : "mail-send"
                        spinning: root.sending
                        menuItems: [
                            {
                                text: i18n("Send as mass mail"),
                                icon: "mail-send"
                            },
                            {
                                text: i18n("Send Later"),
                                icon: "appointment-new"
                            }
                        ]
                        text: root.sending ? i18n("Sending…") : i18n("Send")

                        onTriggered: actionText => {
                            if (root.sending)
                                return;
                            if (actionText === "")
                                root._doSend();
                        // TODO: mass mail / send later
                        }
                    }

                    Kirigami.Icon {
                        Layout.alignment: Qt.AlignVCenter
                        color: Kirigami.Theme.disabledTextColor
                        implicitHeight: 16
                        implicitWidth: 16
                        isMask: true
                        source: "user-identity"
                    }

                    QQC2.ComboBox {
                        id: accountCombo

                        readonly property int edgeInset: 6
                        readonly property int menuGap: 2
                        readonly property int arrowSize: 12

                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredHeight: root._rowH - 8
                        Layout.preferredWidth: 300
                        displayText: currentIndex >= 0 && model && model.length > currentIndex ? root._accountDisplayText(model[currentIndex]) : i18n("Select account")
                        font.pixelSize: 12
                        model: root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []

                        background: Rectangle {
                            color: accountCombo.hovered ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.22) : Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.09)
                            radius: 4

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.top: parent.top
                                color: sysPalette.window
                                opacity: 0.95
                                width: 1
                                x: parent.width - accountCombo.arrowSize - accountCombo.menuGap * 2 - 1
                            }
                        }
                        contentItem: Text {
                            clip: true
                            color: Kirigami.Theme.textColor
                            font: accountCombo.font
                            leftPadding: 8
                            rightPadding: accountCombo.arrowSize + accountCombo.menuGap * 2 + accountCombo.edgeInset
                            text: accountCombo.displayText
                            verticalAlignment: Text.AlignVCenter
                        }
                        delegate: QQC2.ItemDelegate {
                            required property int index
                            required property var modelData

                            font.pixelSize: 12
                            highlighted: accountCombo.highlightedIndex === index
                            text: root._accountDisplayText(modelData)
                            width: accountCombo.width
                        }
                        indicator: Kirigami.Icon {
                            anchors.right: parent.right
                            anchors.rightMargin: accountCombo.menuGap
                            anchors.verticalCenter: parent.verticalCenter
                            color: Kirigami.Theme.textColor
                            height: accountCombo.arrowSize
                            isMask: true
                            source: "arrow-down"
                            width: accountCombo.arrowSize
                        }
                        popup: QQC2.Popup {
                            padding: 4
                            width: accountCombo.width
                            y: accountCombo.height

                            background: Rectangle {
                                border.color: root._border
                                color: root._bg2
                                radius: 4
                            }
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: accountCombo.delegateModel
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                color: Qt.lighter(root._bg, 1.25)
                height: 1
            }

            // ── Fields: To / Cc / Bcc / Subject ───────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RecipientRow {
                    id: toRow

                    Layout.fillWidth: true
                    accessoryText: i18n("Add Cc & Bcc")
                    chipModel: toChipModel
                    dataStoreObj: root.dataStoreObj
                    label: i18n("To:")
                    labelWidth: root._lblW
                    rowHeight: root._rowH
                    showAccessory: !root.showCcBcc

                    onAccessoryClicked: {
                        root.showCcBcc = true;
                        ccRow.focusInput();
                    }
                    onTabPressed: subjectField.forceActiveFocus()
                }
                RecipientRow {
                    id: ccRow

                    Layout.fillWidth: true
                    chipModel: ccChipModel
                    dataStoreObj: root.dataStoreObj
                    label: i18n("Cc:")
                    labelWidth: root._lblW
                    rowHeight: root._rowH
                    visible: root.showCcBcc

                    onTabPressed: bccRow.focusInput()
                }
                RecipientRow {
                    id: bccRow

                    Layout.fillWidth: true
                    chipModel: bccChipModel
                    dataStoreObj: root.dataStoreObj
                    label: i18n("Bcc:")
                    labelWidth: root._lblW
                    rowHeight: root._rowH
                    visible: root.showCcBcc

                    onTabPressed: subjectField.forceActiveFocus()
                }

                // Subject row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root._rowH
                    spacing: 0

                    Item {
                        Layout.preferredHeight: root._rowH
                        Layout.preferredWidth: root._lblW

                        Text {
                            color: Kirigami.Theme.disabledTextColor
                            font.pixelSize: 12
                            text: i18n("Subject:")

                            anchors {
                                left: parent.left
                                leftMargin: 12
                                verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.bottomMargin: 4
                        Layout.fillWidth: true
                        Layout.preferredHeight: root._rowH - 8
                        Layout.rightMargin: 8
                        Layout.topMargin: 4
                        border.color: subjectField.activeFocus ? sysPalette.highlight : Qt.rgba(root._border.r, root._border.g, root._border.b, 0.35)
                        border.width: 1
                        color: "transparent"
                        radius: 4

                        QQC2.TextField {
                            id: subjectField

                            color: Kirigami.Theme.textColor
                            font.pixelSize: 13
                            leftPadding: 6
                            placeholderText: i18n("Subject")
                            placeholderTextColor: Kirigami.Theme.disabledTextColor

                            background: Item {
                            }

                            Keys.onTabPressed: bodyArea.forceActiveFocus()

                            anchors {
                                fill: parent
                                margins: 1
                            }
                        }
                    }
                }
            }

            // ── Attachment chips strip (visible when files are attached) ───
            Flow {
                id: attachStrip

                Layout.bottomMargin: attachmentModel.count > 0 ? 4 : 0
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 8
                Layout.topMargin: attachmentModel.count > 0 ? 4 : 0
                spacing: 6
                visible: attachmentModel.count > 0

                Repeater {
                    model: attachmentModel

                    delegate: AttachmentCard {
                        id: composeChip

                        required property int index
                        required property var modelData

                        attachmentName: modelData.filename
                        showRemoveButton: true

                        onRemoveClicked: attachmentModel.remove(index)

                        AttachmentHoverPopup {
                            anchorItem: composeChip
                            arrowLeftPx: Math.max(24, composeChip.width * 0.25)
                            downloadComplete: true
                            downloadProgress: 100
                            fallbackIcon: composeChip._icon
                            openButtonText: i18n("Open")
                            previewMimeType: ""
                            previewSource: composeChip.modelData.path
                            saveButtonText: i18n("Remove")
                            targetHovered: composeChip.hovered

                            onOpenTriggered: Qt.openUrlExternally("file://" + encodeURI(composeChip.modelData.path))
                            onSaveTriggered: attachmentModel.remove(composeChip.index)
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                color: Qt.lighter(root._bg, 1.25)
                height: 1
            }

            // ── Formatting toolbar ─────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: root._bg2

                RowLayout {
                    spacing: 1

                    anchors {
                        fill: parent
                        leftMargin: 6
                        rightMargin: 6
                    }
                    TbBtn {
                        ico: "mail-attachment"
                        tip: i18n("Attach")

                        onAct: attachDialog.open()
                    }
                    Rectangle {
                        color: root._border
                        height: 20
                        opacity: 0.5
                        width: 1
                    }
                    TbBtn {
                        active: root.fmtBold
                        ico: "format-text-bold"
                        tip: i18n("Bold")

                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
                            if (s !== e)
                                root._applyFormat("<b>", "</b>");
                            root.fmtBold = !root.fmtBold;
                        }
                    }
                    TbBtn {
                        active: root.fmtItalic
                        ico: "format-text-italic"
                        tip: i18n("Italic")

                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
                            if (s !== e)
                                root._applyFormat("<i>", "</i>");
                            root.fmtItalic = !root.fmtItalic;
                        }
                    }
                    TbBtn {
                        active: root.fmtUnderline
                        ico: "format-text-underline"
                        tip: i18n("Underline")

                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
                            if (s !== e)
                                root._applyFormat("<u>", "</u>");
                            root.fmtUnderline = !root.fmtUnderline;
                        }
                    }
                    TbBtn {
                        active: root.fmtStrike
                        ico: "format-text-strikethrough"
                        tip: i18n("Strikethrough")

                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
                            if (s !== e)
                                root._applyFormat("<s>", "</s>");
                            root.fmtStrike = !root.fmtStrike;
                        }
                    }
                    Rectangle {
                        color: root._border
                        height: 20
                        opacity: 0.5
                        width: 1
                    }
                    TbBtn {
                        ico: "format-list-unordered"
                        tip: i18n("Bullet list")

                        onAct: root._applyLinePrefix("• ")
                    }
                    TbBtn {
                        ico: "format-list-ordered"
                        tip: i18n("Numbered list")

                        onAct: root._applyLinePrefix("1. ")
                    }
                    Rectangle {
                        color: root._border
                        height: 20
                        opacity: 0.5
                        width: 1
                    }
                    TbBtn {
                        ico: "format-justify-left"
                        tip: i18n("Align left")

                        onAct: root._applyFormat('<div align="left">', '</div>')
                    }
                    TbBtn {
                        ico: "format-justify-center"
                        tip: i18n("Align center")

                        onAct: root._applyFormat('<div align="center">', '</div>')
                    }
                    TbBtn {
                        ico: "format-justify-right"
                        tip: i18n("Align right")

                        onAct: root._applyFormat('<div align="right">', '</div>')
                    }
                    Rectangle {
                        color: root._border
                        height: 20
                        opacity: 0.5
                        width: 1
                    }
                    TbBtn {
                        ico: "insert-link"
                        tip: i18n("Insert link")

                        onAct: linkBar.visible = !linkBar.visible
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }

            // Link insertion bar
            Rectangle {
                id: linkBar

                Layout.fillWidth: true
                Layout.preferredHeight: linkBar.visible ? 34 : 0
                clip: true
                color: Qt.lighter(root._bg2, 1.06)
                visible: false

                Behavior on Layout.preferredHeight {
                    NumberAnimation {
                        duration: 100
                    }
                }

                RowLayout {
                    spacing: 6

                    anchors {
                        fill: parent
                        leftMargin: 8
                        rightMargin: 8
                    }
                    Text {
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: 12
                        text: i18n("URL:")
                    }
                    QQC2.TextField {
                        id: linkUrlField

                        Layout.fillWidth: true
                        color: Kirigami.Theme.textColor
                        font.pixelSize: 12
                        placeholderText: "https://"

                        background: Rectangle {
                            border.color: root._border
                            border.width: 1
                            color: "transparent"
                            radius: 4
                        }
                    }
                    QQC2.Button {
                        text: i18n("Insert")

                        onClicked: {
                            const url = linkUrlField.text.trim();
                            if (url.length) {
                                const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
                                const sel = s !== e ? bodyArea.selectedText : url;
                                if (s !== e)
                                    bodyArea.remove(s, e);
                                bodyArea.insert(s !== e ? s : bodyArea.cursorPosition, '<a href="' + url + '">' + sel + '</a>');
                            }
                            linkBar.visible = false;
                            linkUrlField.text = "";
                            bodyArea.forceActiveFocus();
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                color: Qt.lighter(root._bg, 1.25)
                height: 1
            }

            // ── Body ───────────────────────────────────────────────────────
            QQC2.ScrollView {
                Layout.fillHeight: true
                Layout.fillWidth: true
                QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AsNeeded
                clip: true

                background: Rectangle {
                    color: root._bg
                }

                QQC2.TextArea {
                    id: bodyArea

                    bottomPadding: 12
                    color: Kirigami.Theme.textColor
                    font.pixelSize: 14
                    leftPadding: 14
                    placeholderText: i18n("Type a message here")
                    placeholderTextColor: Kirigami.Theme.disabledTextColor
                    rightPadding: 14
                    textFormat: TextEdit.RichText
                    topPadding: 12
                    width: parent.width
                    wrapMode: TextEdit.Wrap

                    background: Item {
                    }

                    Keys.onPressed: event => {
                        const hasFormat = root.fmtBold || root.fmtItalic || root.fmtUnderline || root.fmtStrike;
                        if (!hasFormat || event.text.length === 0)
                            return;
                        // Only intercept printable characters (skip backspace=8, tab=9, enter=13, esc=27, del=127, etc.)
                        const code = event.text.charCodeAt(0);
                        if (code <= 32 || code === 127)
                            return;
                        // Filter out Ctrl+key etc. (allow Shift for capitals/symbols)
                        if (event.modifiers & ~(Qt.ShiftModifier | Qt.KeypadModifier))
                            return;
                        event.accepted = true;
                        const s = bodyArea.selectionStart, e = bodyArea.selectionEnd;
                        if (s !== e)
                            bodyArea.remove(s, e);
                        let open = "", close = "";
                        if (root.fmtBold) {
                            open += "<b>";
                            close = "</b>" + close;
                        }
                        if (root.fmtItalic) {
                            open += "<i>";
                            close = "</i>" + close;
                        }
                        if (root.fmtUnderline) {
                            open += "<u>";
                            close = "</u>" + close;
                        }
                        if (root.fmtStrike) {
                            open += "<s>";
                            close = "</s>" + close;
                        }
                        bodyArea.insert(bodyArea.cursorPosition, open + event.text + close);
                    }
                }
            }

            // ── Error bar ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.sendError.length > 0 ? 32 : 0
                clip: true
                color: "#7c2020"
                visible: Layout.preferredHeight > 0

                Behavior on Layout.preferredHeight {
                    NumberAnimation {
                        duration: 120
                    }
                }

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: 10
                        rightMargin: 6
                    }
                    Text {
                        Layout.fillWidth: true
                        color: "white"
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: root.sendError
                    }
                    Text {
                        color: "white"
                        font.pixelSize: 13
                        text: "✕"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor

                            onClicked: root.sendError = ""
                        }
                    }
                }
            }
        }
    }

    // ── Window border overlay — same style as Main.qml ──────────────────────
    Rectangle {
        anchors.fill: parent
        border.color: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
        border.width: 1
        color: "transparent"
        z: 9999
    }

    // ── Resize handles — declared after root chrome so they are on top ──────
    // Edges
    MouseArea {
        cursorShape: Qt.SizeHorCursor
        width: 6

        onPressed: root.startSystemResize(Qt.LeftEdge)

        anchors {
            bottom: parent.bottom
            left: parent.left
            top: parent.top
        }
    }
    MouseArea {
        cursorShape: Qt.SizeHorCursor
        width: 6

        onPressed: root.startSystemResize(Qt.RightEdge)

        anchors {
            bottom: parent.bottom
            right: parent.right
            top: parent.top
        }
    }
    MouseArea {
        cursorShape: Qt.SizeVerCursor
        height: 6

        onPressed: root.startSystemResize(Qt.TopEdge)

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
    }
    MouseArea {
        cursorShape: Qt.SizeVerCursor
        height: 6

        onPressed: root.startSystemResize(Qt.BottomEdge)

        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
    }
    // Corners (declared last so they win over edges)
    MouseArea {
        cursorShape: Qt.SizeFDiagCursor
        height: 12
        width: 12

        onPressed: root.startSystemResize(Qt.TopEdge | Qt.LeftEdge)

        anchors {
            left: parent.left
            top: parent.top
        }
    }
    MouseArea {
        cursorShape: Qt.SizeBDiagCursor
        height: 12
        width: 12

        onPressed: root.startSystemResize(Qt.TopEdge | Qt.RightEdge)

        anchors {
            right: parent.right
            top: parent.top
        }
    }
    MouseArea {
        cursorShape: Qt.SizeBDiagCursor
        height: 12
        width: 12

        onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)

        anchors {
            bottom: parent.bottom
            left: parent.left
        }
    }
    MouseArea {
        cursorShape: Qt.SizeFDiagCursor
        height: 12
        width: 12

        onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge)

        anchors {
            bottom: parent.bottom
            right: parent.right
        }
    }

    // ── Recipient row inline component ─────────────────────────────────────
    component RecipientRow: Item {
        id: rowRoot

        property int _highlightedIndex: -1
        property bool _showSuggestions: false
        property var _suggestions: []
        property string accessoryText: ""
        property var chipModel: null
        property var dataStoreObj: null
        property string label: ""
        property int labelWidth: 72
        property int rowHeight: 36
        property bool showAccessory: false

        signal accessoryClicked
        signal tabPressed

        function _acceptHighlighted() {
            if (_highlightedIndex >= 0 && _highlightedIndex < _suggestions.length) {
                const s = _suggestions[_highlightedIndex];
                chipModel.append({
                    display: s.displayName || s.email,
                    email: s.email
                });
                chipInput.text = "";
                _showSuggestions = false;
                _suggestions = [];
                _highlightedIndex = -1;
                Qt.callLater(() => {
                    chipFlick.contentY = Math.max(0, chipFlick.contentHeight - chipFlick.height);
                });
            }
        }
        function _doAdd(raw) {
            const text = raw.trim();
            if (!text.length)
                return;
            let display = text, email = text;
            const lt = text.lastIndexOf('<'), gt = text.lastIndexOf('>');
            if (lt >= 0 && gt > lt) {
                email = text.slice(lt + 1, gt).trim();
                display = text.slice(0, lt).trim().replace(/^"|"$/g, '');
                if (!display.length)
                    display = email;
            }
            if (rowRoot.chipModel)
                rowRoot.chipModel.append({
                    display: display,
                    email: email
                });
            chipInput.text = "";
            _showSuggestions = false;
            _suggestions = [];
            Qt.callLater(() => {
                chipFlick.contentY = Math.max(0, chipFlick.contentHeight - chipFlick.height);
            });
        }
        function _search(prefix) {
            if (!dataStoreObj || prefix.trim().length < 1) {
                _showSuggestions = false;
                _suggestions = [];
                _highlightedIndex = -1;
                return;
            }
            const res = dataStoreObj.searchContacts(prefix.trim(), 8);
            _suggestions = res;
            _showSuggestions = res.length > 0;
            _highlightedIndex = -1;
        }
        function commitInput() {
            const t = chipInput.text.trim();
            if (t.length)
                _doAdd(t);
        }
        function focusInput() {
            chipInput.forceActiveFocus();
        }

        // Grow to fit up to 3 lines of chips; Flickable scrolls when content exceeds that
        implicitHeight: Math.max(rowRoot.rowHeight, Math.min(chipFlow.implicitHeight + 16, rowRoot.rowHeight * 3))

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Label column — pinned to first-row vertical position
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: rowRoot.labelWidth

                Text {
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: 12
                    text: rowRoot.label

                    anchors {
                        left: parent.left
                        leftMargin: 12
                        top: parent.top
                        topMargin: Math.max(0, Math.round((rowRoot.rowHeight - 22) / 2))
                    }
                }
            }

            // Border rect — grows with chip content
            Rectangle {
                id: chipBorder

                Layout.bottomMargin: 4
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.rightMargin: 8
                Layout.topMargin: 4
                border.color: chipInput.activeFocus ? sysPalette.highlight : Qt.rgba(root._border.r, root._border.g, root._border.b, 0.35)
                border.width: 1
                clip: true
                color: "transparent"
                radius: 4

                // "Add Cc & Bcc" inside the top-right of the input box
                Text {
                    id: accLabel

                    color: sysPalette.highlight
                    font.pixelSize: 11
                    text: rowRoot.accessoryText
                    visible: rowRoot.showAccessory

                    anchors {
                        right: parent.right
                        rightMargin: 8
                        top: parent.top
                        topMargin: 7
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor

                        onClicked: rowRoot.accessoryClicked()
                    }
                }
                Flickable {
                    id: chipFlick

                    clip: true
                    contentHeight: Math.max(height, chipFlow.implicitHeight)
                    contentWidth: width
                    flickableDirection: Flickable.VerticalFlick

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                        policy: QQC2.ScrollBar.AsNeeded
                        visible: chipFlick.contentHeight > chipFlick.height + 1
                    }

                    anchors {
                        bottomMargin: 4
                        fill: parent
                        leftMargin: 4
                        rightMargin: rowRoot.showAccessory ? accLabel.implicitWidth + 16 : 4
                        topMargin: 4
                    }
                    Flow {
                        id: chipFlow

                        spacing: 4
                        width: chipFlick.width

                        Repeater {
                            id: chipRepeater

                            model: rowRoot.chipModel

                            delegate: Rectangle {
                                required property int index
                                required property var modelData

                                color: sysPalette.highlight
                                height: 22
                                radius: 4
                                width: chipLabel.implicitWidth + chipX.implicitWidth + 16

                                Row {
                                    spacing: 4

                                    anchors {
                                        fill: parent
                                        leftMargin: 6
                                        rightMargin: 4
                                    }
                                    Text {
                                        id: chipLabel

                                        anchors.verticalCenter: parent.verticalCenter
                                        color: sysPalette.highlightedText
                                        font.pixelSize: 12
                                        text: modelData.display || modelData.email
                                    }
                                    Text {
                                        id: chipX

                                        anchors.verticalCenter: parent.verticalCenter
                                        color: sysPalette.highlightedText
                                        font.pixelSize: 13
                                        opacity: 0.8
                                        text: "×"

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor

                                            onClicked: rowRoot.chipModel.remove(index)
                                        }
                                    }
                                }
                            }
                        }

                        // Input placed inline after the chips
                        TextInput {
                            id: chipInput

                            clip: true
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 13
                            height: 22
                            selectByMouse: true
                            verticalAlignment: TextInput.AlignVCenter
                            // Fill full width when empty; otherwise compact minimum so Flow can wrap it
                            width: chipRepeater.count === 0 ? chipFlow.width : 80

                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Down && rowRoot._showSuggestions) {
                                    event.accepted = true;
                                    rowRoot._highlightedIndex = Math.min(rowRoot._highlightedIndex + 1, rowRoot._suggestions.length - 1);
                                } else if (event.key === Qt.Key_Up && rowRoot._showSuggestions) {
                                    event.accepted = true;
                                    rowRoot._highlightedIndex = Math.max(rowRoot._highlightedIndex - 1, -1);
                                } else if (event.key === Qt.Key_Comma || event.key === Qt.Key_Semicolon) {
                                    event.accepted = true;
                                    rowRoot._doAdd(text);
                                } else if (event.key === Qt.Key_Backspace && text.length === 0 && rowRoot.chipModel && rowRoot.chipModel.count > 0) {
                                    rowRoot.chipModel.remove(rowRoot.chipModel.count - 1);
                                } else if (event.key === Qt.Key_Escape) {
                                    rowRoot._showSuggestions = false;
                                    rowRoot._suggestions = [];
                                    rowRoot._highlightedIndex = -1;
                                }
                            }
                            Keys.onReturnPressed: event => {
                                if (rowRoot._highlightedIndex >= 0) {
                                    event.accepted = true;
                                    rowRoot._acceptHighlighted();
                                } else {
                                    rowRoot._doAdd(text);
                                }
                            }
                            Keys.onTabPressed: {
                                rowRoot._doAdd(text);
                                rowRoot.tabPressed();
                                event.accepted = true;
                            }
                            onTextChanged: rowRoot._search(text)

                            Text {
                                anchors.fill: parent
                                color: Kirigami.Theme.disabledTextColor
                                font.pixelSize: 13
                                text: (rowRoot.chipModel && rowRoot.chipModel.count === 0 && !chipInput.activeFocus) ? i18n("Type a name or address") : ""
                                verticalAlignment: Text.AlignVCenter
                                visible: chipInput.text.length === 0
                            }
                        }
                    }
                }
            }
        }

        // Autocomplete popup
        QQC2.Popup {
            closePolicy: QQC2.Popup.NoAutoClose
            modal: false
            padding: 4
            parent: rowRoot
            visible: rowRoot._showSuggestions && rowRoot._suggestions.length > 0
            width: rowRoot.width - rowRoot.labelWidth - 16
            x: rowRoot.labelWidth
            y: rowRoot.implicitHeight

            background: Rectangle {
                border.color: root._border
                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.08)
                radius: 4
            }
            contentItem: Column {
                spacing: 1

                Repeater {
                    model: rowRoot._suggestions

                    delegate: Rectangle {
                        required property int index
                        readonly property bool isHighlighted: sugMouse.containsMouse || rowRoot._highlightedIndex === index
                        required property var modelData

                        color: isHighlighted ? sysPalette.highlight : "transparent"
                        height: 30
                        radius: 3
                        width: parent.width

                        Text {
                            color: isHighlighted ? sysPalette.highlightedText : Kirigami.Theme.textColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: modelData.displayName ? modelData.displayName + "  <" + modelData.email + ">" : modelData.email
                            width: parent.width - 16

                            anchors {
                                left: parent.left
                                leftMargin: 8
                                verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            id: sugMouse

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true

                            onClicked: {
                                rowRoot.chipModel.append({
                                    display: modelData.displayName || modelData.email,
                                    email: modelData.email
                                });
                                chipInput.text = "";
                                rowRoot._showSuggestions = false;
                                rowRoot._suggestions = [];
                                rowRoot._highlightedIndex = -1;
                                chipInput.forceActiveFocus();
                            }
                        }
                    }
                }
            }
        }
    }
    component TbBtn: Rectangle {
        property bool active: false
        property string ico: ""
        property string tip: ""

        signal act

        QQC2.ToolTip.delay: 600
        QQC2.ToolTip.text: tip
        QQC2.ToolTip.visible: tbMouse.containsMouse
        color: active ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.3) : tbMouse.pressed ? Qt.lighter(root._bg2, 1.2) : tbMouse.containsMouse ? Qt.lighter(root._bg2, 1.12) : "transparent"
        height: 26
        radius: 4
        width: 28

        Kirigami.Icon {
            anchors.centerIn: parent
            color: parent.active ? sysPalette.highlight : Kirigami.Theme.textColor
            implicitHeight: root._icoSz + 2
            implicitWidth: root._icoSz + 2
            isMask: true
            source: ico
        }
        MouseArea {
            id: tbMouse

            anchors.fill: parent
            hoverEnabled: true

            onClicked: parent.act()
        }
    }
}
