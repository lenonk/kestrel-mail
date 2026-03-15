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

    width:  840
    height: 660
    minimumWidth:  560
    minimumHeight: 420
    visible: false
    title: subjectField.text.trim().length > 0
           ? subjectField.text.trim() + " – " + i18n("New Message")
           : i18n("[no subject] – New Message")
    color: Kirigami.Theme.backgroundColor
    flags: Qt.Window | Qt.FramelessWindowHint

    // ── Public API ─────────────────────────────────────────────────────────
    property var  accountRepositoryObj: null
    property var  dataStoreObj: null
    property var  smtpServiceObj: null

    signal sendRequested()

    function openCompose(to, subject, body) {
        toChipModel.clear()
        ccChipModel.clear()
        bccChipModel.clear()
        subjectField.text = subject || ""
        bodyArea.text = body || ""
        showCcBcc = false
        sendError = ""
        sending = false
        fmtBold = false; fmtItalic = false; fmtUnderline = false; fmtStrike = false
        attachmentModel.clear()
        if (to && to.trim().length > 0) _addChipToModel(toChipModel, to.trim())
        root.show(); root.raise(); root.requestActivate()
        if (toChipModel.count === 0) toRow.focusInput()
        else subjectField.forceActiveFocus()
    }

    function openComposeReply(params) {
        toChipModel.clear()
        ccChipModel.clear()
        bccChipModel.clear()
        subjectField.text = params.subject || ""
        bodyArea.text = params.body || ""
        showCcBcc = !!(params.ccList && params.ccList.length > 0)
        sendError = ""
        sending = false
        fmtBold = false; fmtItalic = false; fmtUnderline = false; fmtStrike = false
        attachmentModel.clear()
        const toList = params.toList || []
        for (let i = 0; i < toList.length; i++) _addChipToModel(toChipModel, toList[i])
        if (params.ccList) {
            for (let i = 0; i < params.ccList.length; i++) _addChipToModel(ccChipModel, params.ccList[i])
        }
        root.show(); root.raise(); root.requestActivate()
        if (toChipModel.count === 0) toRow.focusInput()
        else subjectField.forceActiveFocus()
    }

    // ── Internal state ─────────────────────────────────────────────────────
    SystemPalette { id: sysPalette }

    ListModel { id: toChipModel     }
    ListModel { id: ccChipModel     }
    ListModel { id: bccChipModel    }
    ListModel { id: attachmentModel }

    property bool   showCcBcc: false
    property bool   sending:   false
    property string sendError: ""

    // Format toggle states
    property bool fmtBold:      false
    property bool fmtItalic:    false
    property bool fmtUnderline: false
    property bool fmtStrike:    false

    readonly property int   _titleH: Kirigami.Units.gridUnit + 12
    readonly property int   _btnW:   Kirigami.Units.gridUnit + 16
    readonly property int   _btnH:   Kirigami.Units.gridUnit + 5
    readonly property int   _icoSz:  Kirigami.Units.gridUnit - 4
    readonly property int   _rowH:   36
    readonly property int   _lblW:   72
    readonly property color _bg:     Kirigami.Theme.backgroundColor
    readonly property color _bg2:    Qt.lighter(_bg, 1.08)
    readonly property color _border: Kirigami.Theme.disabledTextColor

    // ── Helpers ────────────────────────────────────────────────────────────
    function _addChipToModel(model, raw) {
        const text = raw.trim(); if (!text.length) return
        let display = text, email = text
        const lt = text.lastIndexOf('<'); const gt = text.lastIndexOf('>')
        if (lt >= 0 && gt > lt) {
            email   = text.slice(lt + 1, gt).trim()
            display = text.slice(0, lt).trim().replace(/^"|"$/g, '')
            if (!display.length) display = email
        }
        model.append({ display: display, email: email })
    }

    function _chipListToStrings(model) {
        const arr = []
        for (let i = 0; i < model.count; i++)
            arr.push('"' + model.get(i).display + '" <' + model.get(i).email + '>')
        return arr
    }

    function _doSend() {
        if (!smtpServiceObj) { sendError = i18n("SMTP service not available"); return }
        toRow.commitInput(); ccRow.commitInput(); bccRow.commitInput()
        if (toChipModel.count === 0) { sendError = i18n("Please add at least one recipient"); return }
        sending = true; sendError = ""
        const fromEmail = accountCombo.currentIndex >= 0 && accountRepositoryObj
            ? accountRepositoryObj.accounts[accountCombo.currentIndex].email : ""
        const attachPaths = []
        for (let i = 0; i < attachmentModel.count; i++)
            attachPaths.push(attachmentModel.get(i).path)
        smtpServiceObj.sendEmail({
            fromEmail:   fromEmail,
            toList:      _chipListToStrings(toChipModel),
            ccList:      _chipListToStrings(ccChipModel),
            bccList:     _chipListToStrings(bccChipModel),
            subject:     subjectField.text,
            body:        bodyArea.text,
            attachments: attachPaths
        })
    }

    function _applyFormat(open, close) {
        const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
        if (s === e) {
            bodyArea.insert(bodyArea.cursorPosition, open + close)
            bodyArea.cursorPosition -= close.length
        } else {
            const sel = bodyArea.selectedText
            bodyArea.remove(s, e)
            bodyArea.insert(s, open + sel + close)
        }
        bodyArea.forceActiveFocus()
    }

    function _applyLinePrefix(linePrefix) {
        const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
        const sel = s === e ? "" : bodyArea.selectedText
        if (!sel.length) {
            bodyArea.insert(bodyArea.cursorPosition, linePrefix)
        } else {
            const lines = sel.split("\n")
            bodyArea.remove(s, e)
            bodyArea.insert(s, lines.map(l => linePrefix + l).join("\n"))
        }
        bodyArea.forceActiveFocus()
    }

    Connections {
        target: root.smtpServiceObj
        function onSendFinished(ok, message) {
            sending = false
            if (ok) { root.hide(); root.sendRequested() }
            else     { sendError = message }
        }
    }

    FileDialog {
        id: attachDialog
        title: i18n("Attach File")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (let i = 0; i < selectedFiles.length; i++) {
                const url = selectedFiles[i].toString()
                const path = decodeURIComponent(url.replace(/^file:\/\//, ""))
                const filename = path.split("/").pop()
                attachmentModel.append({ filename: filename, path: path })
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
                    anchors { fill: parent; rightMargin: root._btnW * 3 + 4; leftMargin: root._btnW + 4 }
                    acceptedButtons: Qt.LeftButton
                    onPressed: root.startSystemMove()
                }
                TitleBarIconButton {
                    anchors { left: parent.left; leftMargin: Kirigami.Units.smallSpacing; verticalCenter: parent.verticalCenter }
                    buttonWidth: root._btnW; buttonHeight: root._btnH; iconSize: root._icoSz
                    highlightColor: sysPalette.highlight
                    iconName: "open-menu-symbolic"
                    onClicked: composeMenu.openIfClosed()
                }
                QQC2.Label {
                    anchors.centerIn: parent
                    text: root.title
                    font.bold: false; font.pixelSize: 12
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, parent.width - root._btnW * 5)
                }
                Row {
                    anchors { right: parent.right; rightMargin: Kirigami.Units.smallSpacing; verticalCenter: parent.verticalCenter }
                    spacing: 2
                    TitleBarIconButton {
                        buttonWidth: root._btnW; buttonHeight: root._btnH; iconSize: root._icoSz
                        highlightColor: sysPalette.highlight; iconName: "window-minimize-symbolic"
                        onClicked: root.showMinimized()
                    }
                    TitleBarIconButton {
                        buttonWidth: root._btnW; buttonHeight: root._btnH; iconSize: root._icoSz
                        highlightColor: sysPalette.highlight
                        iconName: root.visibility === Window.Maximized ? "window-restore-symbolic" : "window-maximize-symbolic"
                        onClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                    }
                    TitleBarIconButton {
                        buttonWidth: root._btnW; buttonHeight: root._btnH; iconSize: root._icoSz
                        highlightColor: sysPalette.highlight; iconName: "window-close-symbolic"
                        onClicked: root.hide()
                    }
                }
                PopupMenu {
                    id: composeMenu
                    parent: root
                    x: Kirigami.Units.smallSpacing
                    y: root._titleH
                    QQC2.MenuItem { text: i18n("Save Draft") }
                    QQC2.MenuItem { text: i18n("Discard") }
                    QQC2.MenuSeparator {}
                    QQC2.MenuItem { text: i18n("Settings") }
                }
            }

            // ── Action bar: Send + From ────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root._titleH + 6
                color: root._bg2

                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    spacing: 8

                    MailActionButton {
                        id: sendBtn
                        iconName: "mail-send"
                        text: root.sending ? i18n("Sending…") : i18n("Send")
                        alwaysHighlighted: true
                        enabled: !root.sending
                        menuItems: [
                            { text: i18n("Send as mass mail"), icon: "mail-send" },
                            { text: i18n("Send Later"),        icon: "appointment-new" }
                        ]
                        onTriggered: (actionText) => {
                            if (actionText === "") root._doSend()
                            // TODO: mass mail / send later
                        }
                    }

                    Kirigami.Icon {
                        source: "user-identity"; color: Kirigami.Theme.disabledTextColor
                        implicitWidth: 16; implicitHeight: 16; isMask: true
                    }

                    QQC2.ComboBox {
                        id: accountCombo
                        Layout.preferredWidth: 300
                        Layout.preferredHeight: root._rowH - 8
                        Layout.alignment: Qt.AlignVCenter
                        model: root.accountRepositoryObj ? root.accountRepositoryObj.accounts : []
                        displayText: currentIndex >= 0 && model && model.length > currentIndex
                                     ? '"' + model[currentIndex].accountName + '" <' + model[currentIndex].email + '>'
                                     : i18n("Select account")
                        font.pixelSize: 12
                        delegate: QQC2.ItemDelegate {
                            required property var modelData
                            required property int index
                            width: accountCombo.width
                            text: '"' + modelData.accountName + '" <' + modelData.email + '>'
                            font.pixelSize: 12
                            highlighted: accountCombo.highlightedIndex === index
                        }
                        indicator: Kirigami.Icon {
                            source: "arrow-down"
                            width: 12; height: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            color: Kirigami.Theme.textColor
                            isMask: true
                        }
                        background: Rectangle {
                            color: accountCombo.hovered
                                   ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.22)
                                   : Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.09)
                            radius: 4
                        }
                        contentItem: Text {
                            text: accountCombo.displayText; color: Kirigami.Theme.textColor
                            font: accountCombo.font; elide: Text.ElideRight
                            leftPadding: 8; rightPadding: 24; verticalAlignment: Text.AlignVCenter
                        }
                        popup: QQC2.Popup {
                            y: accountCombo.height; width: accountCombo.width; padding: 4
                            background: Rectangle { color: root._bg2; border.color: root._border; radius: 4 }
                            contentItem: ListView { implicitHeight: contentHeight; model: accountCombo.delegateModel; clip: true }
                        }
                    }

                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.lighter(root._bg, 1.25) }

            // ── Fields: To / Cc / Bcc / Subject ───────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RecipientRow {
                    id: toRow
                    Layout.fillWidth: true
                    label: i18n("To")
                    chipModel: toChipModel
                    dataStoreObj: root.dataStoreObj
                    rowHeight: root._rowH
                    labelWidth: root._lblW
                    showAccessory: !root.showCcBcc
                    accessoryText: i18n("Add Cc & Bcc")
                    onAccessoryClicked: { root.showCcBcc = true; ccRow.focusInput() }
                    onTabPressed: subjectField.forceActiveFocus()
                }

                RecipientRow {
                    id: ccRow
                    Layout.fillWidth: true
                    visible: root.showCcBcc
                    label: i18n("Cc")
                    chipModel: ccChipModel
                    dataStoreObj: root.dataStoreObj
                    rowHeight: root._rowH
                    labelWidth: root._lblW
                    onTabPressed: bccRow.focusInput()
                }

                RecipientRow {
                    id: bccRow
                    Layout.fillWidth: true
                    visible: root.showCcBcc
                    label: i18n("Bcc")
                    chipModel: bccChipModel
                    dataStoreObj: root.dataStoreObj
                    rowHeight: root._rowH
                    labelWidth: root._lblW
                    onTabPressed: subjectField.forceActiveFocus()
                }

                // Subject row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root._rowH
                    spacing: 0

                    Item {
                        Layout.preferredWidth: root._lblW
                        Layout.preferredHeight: root._rowH
                        Text {
                            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12 }
                            text: i18n("Subject")
                            color: Kirigami.Theme.disabledTextColor
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root._rowH - 8
                        Layout.alignment: Qt.AlignVCenter
                        Layout.topMargin: 4; Layout.bottomMargin: 4
                        Layout.rightMargin: 8
                        color: "transparent"
                        border.color: subjectField.activeFocus ? sysPalette.highlight
                                      : Qt.rgba(root._border.r, root._border.g, root._border.b, 0.35)
                        border.width: 1
                        radius: 4

                        QQC2.TextField {
                            id: subjectField
                            anchors { fill: parent; margins: 1 }
                            background: Item {}
                            color: Kirigami.Theme.textColor
                            placeholderTextColor: Kirigami.Theme.disabledTextColor
                            placeholderText: i18n("Subject")
                            font.pixelSize: 13
                            leftPadding: 6
                            Keys.onTabPressed: bodyArea.forceActiveFocus()
                        }
                    }
                }
            }

            // ── Attachment chips strip (visible when files are attached) ───
            Flow {
                id: attachStrip
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 8
                Layout.topMargin: attachmentModel.count > 0 ? 4 : 0
                Layout.bottomMargin: attachmentModel.count > 0 ? 4 : 0
                visible: attachmentModel.count > 0
                spacing: 6

                Repeater {
                    model: attachmentModel
                    delegate: AttachmentCard {
                        id: composeChip
                        required property var modelData
                        required property int index
                        attachmentName: modelData.filename
                        showRemoveButton: true
                        onRemoveClicked: attachmentModel.remove(index)

                        AttachmentHoverPopup {
                            anchorItem:       composeChip
                            arrowLeftPx:      Math.max(24, composeChip.width * 0.25)
                            downloadComplete: true
                            downloadProgress: 100
                            fallbackIcon:     composeChip._icon
                            openButtonText:   i18n("Open")
                            previewMimeType:  ""
                            previewSource:    composeChip.modelData.path
                            saveButtonText:   i18n("Remove")
                            targetHovered:    composeChip.hovered

                            onOpenTriggered: Qt.openUrlExternally("file://" + encodeURI(composeChip.modelData.path))
                            onSaveTriggered: attachmentModel.remove(composeChip.index)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.lighter(root._bg, 1.25) }

            // ── Formatting toolbar ─────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: root._bg2

                RowLayout {
                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                    spacing: 1

                    component TbBtn: Rectangle {
                        property string tip: ""
                        property string ico: ""
                        property bool   active: false
                        signal act()

                        width: 28; height: 26; radius: 4
                        color: active
                               ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.3)
                               : tbMouse.pressed ? Qt.lighter(root._bg2, 1.2)
                               : tbMouse.containsMouse ? Qt.lighter(root._bg2, 1.12) : "transparent"
                        QQC2.ToolTip.text: tip
                        QQC2.ToolTip.visible: tbMouse.containsMouse
                        QQC2.ToolTip.delay: 600
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            source: ico; isMask: true
                            color: parent.active ? sysPalette.highlight : Kirigami.Theme.textColor
                            implicitWidth: root._icoSz + 2
                            implicitHeight: root._icoSz + 2
                        }
                        MouseArea { id: tbMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.act() }
                    }

                    TbBtn { ico: "mail-attachment"; tip: i18n("Attach"); onAct: attachDialog.open() }

                    Rectangle { width: 1; height: 20; color: root._border; opacity: 0.5 }

                    TbBtn {
                        ico: "format-text-bold"; tip: i18n("Bold"); active: root.fmtBold
                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
                            if (s !== e) root._applyFormat("<b>", "</b>")
                            root.fmtBold = !root.fmtBold
                        }
                    }
                    TbBtn {
                        ico: "format-text-italic"; tip: i18n("Italic"); active: root.fmtItalic
                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
                            if (s !== e) root._applyFormat("<i>", "</i>")
                            root.fmtItalic = !root.fmtItalic
                        }
                    }
                    TbBtn {
                        ico: "format-text-underline"; tip: i18n("Underline"); active: root.fmtUnderline
                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
                            if (s !== e) root._applyFormat("<u>", "</u>")
                            root.fmtUnderline = !root.fmtUnderline
                        }
                    }
                    TbBtn {
                        ico: "format-text-strikethrough"; tip: i18n("Strikethrough"); active: root.fmtStrike
                        onAct: {
                            const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
                            if (s !== e) root._applyFormat("<s>", "</s>")
                            root.fmtStrike = !root.fmtStrike
                        }
                    }

                    Rectangle { width: 1; height: 20; color: root._border; opacity: 0.5 }

                    TbBtn { ico: "format-list-unordered"; tip: i18n("Bullet list");   onAct: root._applyLinePrefix("• ") }
                    TbBtn { ico: "format-list-ordered";   tip: i18n("Numbered list"); onAct: root._applyLinePrefix("1. ") }

                    Rectangle { width: 1; height: 20; color: root._border; opacity: 0.5 }

                    TbBtn { ico: "format-justify-left";   tip: i18n("Align left");   onAct: root._applyFormat('<div align="left">',   '</div>') }
                    TbBtn { ico: "format-justify-center"; tip: i18n("Align center"); onAct: root._applyFormat('<div align="center">', '</div>') }
                    TbBtn { ico: "format-justify-right";  tip: i18n("Align right");  onAct: root._applyFormat('<div align="right">',  '</div>') }

                    Rectangle { width: 1; height: 20; color: root._border; opacity: 0.5 }

                    TbBtn { ico: "insert-link"; tip: i18n("Insert link"); onAct: linkBar.visible = !linkBar.visible }

                    Item { Layout.fillWidth: true }
                }
            }

            // Link insertion bar
            Rectangle {
                id: linkBar
                Layout.fillWidth: true
                Layout.preferredHeight: linkBar.visible ? 34 : 0
                visible: false
                color: Qt.lighter(root._bg2, 1.06)
                clip: true
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 100 } }

                RowLayout {
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                    spacing: 6
                    Text { text: i18n("URL:"); color: Kirigami.Theme.disabledTextColor; font.pixelSize: 12 }
                    QQC2.TextField {
                        id: linkUrlField
                        Layout.fillWidth: true
                        placeholderText: "https://"
                        font.pixelSize: 12
                        background: Rectangle { color: "transparent"; border.color: root._border; border.width: 1; radius: 4 }
                        color: Kirigami.Theme.textColor
                    }
                    QQC2.Button {
                        text: i18n("Insert")
                        onClicked: {
                            const url = linkUrlField.text.trim()
                            if (url.length) {
                                const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
                                const sel = s !== e ? bodyArea.selectedText : url
                                if (s !== e) bodyArea.remove(s, e)
                                bodyArea.insert(s !== e ? s : bodyArea.cursorPosition,
                                    '<a href="' + url + '">' + sel + '</a>')
                            }
                            linkBar.visible = false; linkUrlField.text = ""
                            bodyArea.forceActiveFocus()
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.lighter(root._bg, 1.25) }

            // ── Body ───────────────────────────────────────────────────────
            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                background: Rectangle { color: root._bg }
                QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AsNeeded

                QQC2.TextArea {
                    id: bodyArea
                    width: parent.width
                    placeholderText: i18n("Type a message here")
                    placeholderTextColor: Kirigami.Theme.disabledTextColor
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.RichText
                    font.pixelSize: 14
                    color: Kirigami.Theme.textColor
                    background: Item {}
                    topPadding: 12; leftPadding: 14; rightPadding: 14; bottomPadding: 12

                    Keys.onPressed: (event) => {
                        const hasFormat = root.fmtBold || root.fmtItalic || root.fmtUnderline || root.fmtStrike
                        if (!hasFormat || event.text.length === 0) return
                        // Only intercept printable characters (skip backspace=8, tab=9, enter=13, esc=27, del=127, etc.)
                        const code = event.text.charCodeAt(0)
                        if (code <= 32 || code === 127) return
                        // Filter out Ctrl+key etc. (allow Shift for capitals/symbols)
                        if (event.modifiers & ~(Qt.ShiftModifier | Qt.KeypadModifier)) return
                        event.accepted = true
                        const s = bodyArea.selectionStart, e = bodyArea.selectionEnd
                        if (s !== e) bodyArea.remove(s, e)
                        let open = "", close = ""
                        if (root.fmtBold)      { open += "<b>";  close = "</b>"  + close }
                        if (root.fmtItalic)    { open += "<i>";  close = "</i>"  + close }
                        if (root.fmtUnderline) { open += "<u>";  close = "</u>"  + close }
                        if (root.fmtStrike)    { open += "<s>";  close = "</s>"  + close }
                        bodyArea.insert(bodyArea.cursorPosition, open + event.text + close)
                    }
                }
            }

            // ── Error bar ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.sendError.length > 0 ? 32 : 0
                visible: Layout.preferredHeight > 0
                color: "#7c2020"
                clip: true
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 120 } }

                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                    Text {
                        Layout.fillWidth: true
                        text: root.sendError; color: "white"; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    Text {
                        text: "✕"; color: "white"; font.pixelSize: 13
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.sendError = "" }
                    }
                }
            }
        }
    }

    // ── Window border overlay — same style as Main.qml ──────────────────────
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
        z: 9999
    }

    // ── Resize handles — declared after root chrome so they are on top ──────
    // Edges
    MouseArea { anchors { left: parent.left;   top: parent.top;    bottom: parent.bottom } width:  6; cursorShape: Qt.SizeHorCursor;  onPressed: root.startSystemResize(Qt.LeftEdge) }
    MouseArea { anchors { right: parent.right; top: parent.top;    bottom: parent.bottom } width:  6; cursorShape: Qt.SizeHorCursor;  onPressed: root.startSystemResize(Qt.RightEdge) }
    MouseArea { anchors { top: parent.top;     left: parent.left;  right: parent.right }   height: 6; cursorShape: Qt.SizeVerCursor;  onPressed: root.startSystemResize(Qt.TopEdge) }
    MouseArea { anchors { bottom: parent.bottom; left: parent.left; right: parent.right }  height: 6; cursorShape: Qt.SizeVerCursor;  onPressed: root.startSystemResize(Qt.BottomEdge) }
    // Corners (declared last so they win over edges)
    MouseArea { anchors { left: parent.left;   top: parent.top }    width: 12; height: 12; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.TopEdge    | Qt.LeftEdge) }
    MouseArea { anchors { right: parent.right; top: parent.top }    width: 12; height: 12; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.TopEdge    | Qt.RightEdge) }
    MouseArea { anchors { left: parent.left;   bottom: parent.bottom } width: 12; height: 12; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
    MouseArea { anchors { right: parent.right; bottom: parent.bottom } width: 12; height: 12; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }

    // ── Recipient row inline component ─────────────────────────────────────
    component RecipientRow: Item {
        id: rowRoot

        property string label: ""
        property var    chipModel: null
        property var    dataStoreObj: null
        property int    rowHeight: 36
        property int    labelWidth: 72
        property bool   showAccessory: false
        property string accessoryText: ""
        signal accessoryClicked()
        signal tabPressed()

        function focusInput()  { chipInput.forceActiveFocus() }
        function commitInput() { const t = chipInput.text.trim(); if (t.length) _doAdd(t) }

        property var  _suggestions: []
        property bool _showSuggestions: false
        property int  _highlightedIndex: -1

        // Grow to fit up to 3 lines of chips; Flickable scrolls when content exceeds that
        implicitHeight: Math.max(rowRoot.rowHeight,
                        Math.min(chipFlow.implicitHeight + 16, rowRoot.rowHeight * 3))

        function _doAdd(raw) {
            const text = raw.trim(); if (!text.length) return
            let display = text, email = text
            const lt = text.lastIndexOf('<'), gt = text.lastIndexOf('>')
            if (lt >= 0 && gt > lt) {
                email   = text.slice(lt + 1, gt).trim()
                display = text.slice(0, lt).trim().replace(/^"|"$/g, '')
                if (!display.length) display = email
            }
            if (rowRoot.chipModel) rowRoot.chipModel.append({ display: display, email: email })
            chipInput.text = ""; _showSuggestions = false; _suggestions = []
            Qt.callLater(() => { chipFlick.contentY = Math.max(0, chipFlick.contentHeight - chipFlick.height) })
        }

        function _search(prefix) {
            if (!dataStoreObj || prefix.trim().length < 1) { _showSuggestions = false; _suggestions = []; _highlightedIndex = -1; return }
            const res = dataStoreObj.searchContacts(prefix.trim(), 8)
            _suggestions = res; _showSuggestions = res.length > 0; _highlightedIndex = -1
        }

        function _acceptHighlighted() {
            if (_highlightedIndex >= 0 && _highlightedIndex < _suggestions.length) {
                const s = _suggestions[_highlightedIndex]
                chipModel.append({ display: s.displayName || s.email, email: s.email })
                chipInput.text = ""; _showSuggestions = false; _suggestions = []; _highlightedIndex = -1
                Qt.callLater(() => { chipFlick.contentY = Math.max(0, chipFlick.contentHeight - chipFlick.height) })
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Label column — pinned to first-row vertical position
            Item {
                Layout.preferredWidth: rowRoot.labelWidth
                Layout.fillHeight: true
                Text {
                    anchors { top: parent.top; topMargin: Math.max(0, Math.round((rowRoot.rowHeight - 22) / 2)); left: parent.left; leftMargin: 12 }
                    text: rowRoot.label
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: 12
                }
            }

            // Border rect — grows with chip content
            Rectangle {
                id: chipBorder
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 4; Layout.bottomMargin: 4
                Layout.rightMargin: 8
                color: "transparent"
                border.color: chipInput.activeFocus ? sysPalette.highlight
                              : Qt.rgba(root._border.r, root._border.g, root._border.b, 0.35)
                border.width: 1
                radius: 4
                clip: true

                // "Add Cc & Bcc" inside the top-right of the input box
                Text {
                    id: accLabel
                    visible: rowRoot.showAccessory
                    anchors { right: parent.right; rightMargin: 8; top: parent.top; topMargin: 7 }
                    text: rowRoot.accessoryText
                    color: sysPalette.highlight
                    font.pixelSize: 11
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rowRoot.accessoryClicked()
                    }
                }

                Flickable {
                    id: chipFlick
                    anchors {
                        fill: parent
                        leftMargin: 4; topMargin: 4; bottomMargin: 4
                        rightMargin: rowRoot.showAccessory ? accLabel.implicitWidth + 16 : 4
                    }
                    flickableDirection: Flickable.VerticalFlick
                    contentWidth: width
                    contentHeight: Math.max(height, chipFlow.implicitHeight)
                    clip: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                        visible: chipFlick.contentHeight > chipFlick.height + 1
                        policy: QQC2.ScrollBar.AsNeeded
                    }

                    Flow {
                        id: chipFlow
                        width: chipFlick.width
                        spacing: 4

                        Repeater {
                            id: chipRepeater
                            model: rowRoot.chipModel
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                height: 22; radius: 4
                                color: sysPalette.highlight
                                width: chipLabel.implicitWidth + chipX.implicitWidth + 16

                                Row {
                                    anchors { fill: parent; leftMargin: 6; rightMargin: 4 }
                                    spacing: 4
                                    Text {
                                        id: chipLabel
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.display || modelData.email
                                        font.pixelSize: 12
                                        color: sysPalette.highlightedText
                                    }
                                    Text {
                                        id: chipX
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "×"; font.pixelSize: 13
                                        color: sysPalette.highlightedText; opacity: 0.8
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
                            height: 22
                            // Fill full width when empty; otherwise compact minimum so Flow can wrap it
                            width: chipRepeater.count === 0 ? chipFlow.width : 80
                            verticalAlignment: TextInput.AlignVCenter
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 13
                            clip: true
                            selectByMouse: true

                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: (rowRoot.chipModel && rowRoot.chipModel.count === 0 && !chipInput.activeFocus)
                                      ? i18n("Type a name or address") : ""
                                color: Kirigami.Theme.disabledTextColor
                                font.pixelSize: 13
                                visible: chipInput.text.length === 0
                            }

                            Keys.onReturnPressed: (event) => {
                                if (rowRoot._highlightedIndex >= 0) {
                                    event.accepted = true; rowRoot._acceptHighlighted()
                                } else {
                                    rowRoot._doAdd(text)
                                }
                            }
                            Keys.onTabPressed: { rowRoot._doAdd(text); rowRoot.tabPressed(); event.accepted = true }
                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Down && rowRoot._showSuggestions) {
                                    event.accepted = true
                                    rowRoot._highlightedIndex = Math.min(rowRoot._highlightedIndex + 1, rowRoot._suggestions.length - 1)
                                } else if (event.key === Qt.Key_Up && rowRoot._showSuggestions) {
                                    event.accepted = true
                                    rowRoot._highlightedIndex = Math.max(rowRoot._highlightedIndex - 1, -1)
                                } else if (event.key === Qt.Key_Comma || event.key === Qt.Key_Semicolon) {
                                    event.accepted = true; rowRoot._doAdd(text)
                                } else if (event.key === Qt.Key_Backspace && text.length === 0
                                        && rowRoot.chipModel && rowRoot.chipModel.count > 0) {
                                    rowRoot.chipModel.remove(rowRoot.chipModel.count - 1)
                                } else if (event.key === Qt.Key_Escape) {
                                    rowRoot._showSuggestions = false; rowRoot._suggestions = []; rowRoot._highlightedIndex = -1
                                }
                            }
                            onTextChanged: rowRoot._search(text)
                        }
                    }
                }
            }
        }

        // Autocomplete popup
        QQC2.Popup {
            parent: rowRoot
            x: rowRoot.labelWidth; y: rowRoot.implicitHeight
            width: rowRoot.width - rowRoot.labelWidth - 16
            padding: 4
            visible: rowRoot._showSuggestions && rowRoot._suggestions.length > 0
            modal: false; closePolicy: QQC2.Popup.NoAutoClose
            background: Rectangle {
                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.08)
                border.color: root._border; radius: 4
            }
            contentItem: Column {
                spacing: 1
                Repeater {
                    model: rowRoot._suggestions
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: parent.width; height: 30
                        readonly property bool isHighlighted: sugMouse.containsMouse || rowRoot._highlightedIndex === index
                        color: isHighlighted ? sysPalette.highlight : "transparent"
                        radius: 3
                        Text {
                            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 8 }
                            text: modelData.displayName
                                  ? modelData.displayName + "  <" + modelData.email + ">"
                                  : modelData.email
                            color: isHighlighted ? sysPalette.highlightedText : Kirigami.Theme.textColor
                            font.pixelSize: 12; elide: Text.ElideRight; width: parent.width - 16
                        }
                        MouseArea {
                            id: sugMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                rowRoot.chipModel.append({ display: modelData.displayName || modelData.email, email: modelData.email })
                                chipInput.text = ""; rowRoot._showSuggestions = false; rowRoot._suggestions = []; rowRoot._highlightedIndex = -1
                                chipInput.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }
    }
}
