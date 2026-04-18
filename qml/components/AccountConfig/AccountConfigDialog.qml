import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

import "../Layout" as AppLayout

Window {
    id: root
    width: 900
    height: 680
    visible: false
    title: i18n("Accounts")
    modality: Qt.ApplicationModal
    color: "transparent"
    flags: Qt.Dialog | Qt.FramelessWindowHint

    property var accountRepositoryObj: null
    property var accountSetupObj: null
    property var accountManagerObj: null

    property string selectedEmail: ""
    property var editedConfig: ({})

    readonly property bool isGmail: {
        var pid = (editedConfig.providerId || editedConfig.provider_id || "").toString().toLowerCase()
        return pid === "gmail"
    }

    SystemPalette { id: systemPalette }

    function open() {
        loadAccountList()
        visible = true
        raise()
        requestActivate()
    }

    function loadAccountList() {
        if (!accountRepositoryObj) return
        var accts = accountRepositoryObj.accounts
        if (accts.length > 0 && !selectedEmail)
            selectedEmail = (accts[0].email || "").toString()
        if (selectedEmail)
            loadAccount(selectedEmail)
    }

    function loadAccount(email) {
        selectedEmail = email
        tabBar.currentIndex = 0
        if (!accountRepositoryObj) return
        var accts = accountRepositoryObj.accounts
        for (var i = 0; i < accts.length; ++i) {
            var a = typeof accts[i].toMap === "function" ? accts[i].toMap() : accts[i]
            if ((a.email || "") === email) {
                editedConfig = Object.assign({}, a)
                return
            }
        }
    }

    function saveAndClose() {
        if (accountRepositoryObj && selectedEmail) {
            var dbMap = {
                email:             editedConfig.email || "",
                accountName:       editedConfig.accountName || editedConfig.account_name || "",
                displayName:       editedConfig.displayName || editedConfig.display_name || "",
                avatarIcon:        editedConfig.avatarIcon || editedConfig.avatar_icon || "",
                providerId:        editedConfig.providerId || editedConfig.provider_id || "",
                providerName:      editedConfig.providerName || editedConfig.provider_name || "",
                authType:          editedConfig.authType || editedConfig.auth_type || "password",
                imapHost:          editedConfig.imapHost || editedConfig.imap_host || "",
                imapPort:          editedConfig.imapPort || editedConfig.imap_port || 993,
                smtpHost:          editedConfig.smtpHost || editedConfig.smtp_host || "",
                smtpPort:          editedConfig.smtpPort || editedConfig.smtp_port || 587,
                encryption:        editedConfig.encryption || "Auto",
                oauthTokenUrl:     editedConfig.oauthTokenUrl || editedConfig.oauth_token_url || "",
                oauthClientId:     editedConfig.oauthClientId || editedConfig.oauth_client_id || "",
                oauthClientSecret: editedConfig.oauthClientSecret || editedConfig.oauth_client_secret || ""
            }
            accountRepositoryObj.addOrUpdateAccount(dbMap)
        }
        visible = false
    }

    function removeSelectedAccount() {
        if (!accountSetupObj || !selectedEmail) return
        accountSetupObj.removeAccount(selectedEmail)
        selectedEmail = ""
        loadAccountList()
    }

    function updateConfig(key, value) {
        var c = Object.assign({}, editedConfig)
        c[key] = value
        editedConfig = c
    }

    function confirmRemoveAccount() {
        if (!selectedEmail) return
        removeConfirmDialog.accountName = editedConfig.accountName || editedConfig.account_name || selectedEmail
        removeConfirmDialog.visible = true
        removeConfirmDialog.raise()
    }

    // ── Remove confirmation dialog ──────────────────────────────
    Window {
        id: removeConfirmDialog
        width: 420
        height: 180
        visible: false
        title: i18n("Remove account")
        modality: Qt.ApplicationModal
        color: "transparent"
        flags: Qt.Dialog | Qt.FramelessWindowHint

        property string accountName: ""

        Kirigami.Page {
            anchors.fill: parent
            leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
            background: Item {}

            Rectangle {
                anchors.fill: parent
                color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Title bar
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: "transparent"

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.SizeAllCursor
                            onPressed: removeConfirmDialog.startSystemMove()
                        }

                        QQC2.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: i18n("Remove account")
                            font.pixelSize: 13
                        }

                        AppLayout.TitleBarIconButton {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            buttonWidth: Kirigami.Units.gridUnit + 12
                            buttonHeight: Kirigami.Units.gridUnit + 4
                            iconSize: Kirigami.Units.gridUnit - 4
                            highlightColor: systemPalette.highlight
                            iconName: "window-close-symbolic"
                            onClicked: removeConfirmDialog.visible = false
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; height: 1
                        color: Qt.darker(Kirigami.Theme.backgroundColor, 1.4)
                    }

                    // Body
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.largeSpacing

                        RowLayout {
                            spacing: Kirigami.Units.largeSpacing
                            Kirigami.Icon {
                                source: "dialog-warning"
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                            }
                            QQC2.Label {
                                text: i18n("Are you sure you want to remove account %1?", removeConfirmDialog.accountName)
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                font.bold: true
                            }
                        }

                        QQC2.Label {
                            text: i18n("Warning: Removing this account will also locally remove all folders and items in this account tree. This will not affect the data synchronized to the server.")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 32 + Kirigami.Units.largeSpacing
                            opacity: 0.8
                        }

                        Item { Layout.fillHeight: true }

                        RowLayout {
                            Layout.alignment: Qt.AlignRight
                            spacing: Kirigami.Units.largeSpacing

                            Rectangle {
                                width: 80; height: 32; radius: 4
                                color: systemPalette.highlight
                                QQC2.Label {
                                    anchors.centerIn: parent
                                    text: i18n("Yes")
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        removeConfirmDialog.visible = false
                                        root.removeSelectedAccount()
                                    }
                                }
                            }
                            Rectangle {
                                width: 80; height: 32; radius: 4
                                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.3)
                                QQC2.Label {
                                    anchors.centerIn: parent
                                    text: i18n("No")
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: removeConfirmDialog.visible = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Tab model changes based on provider
    readonly property var gmailTabs: ["General", "IMAP", "SMTP", "Google Calendar", "Diagnostics"]
    readonly property var genericTabs: ["General", "IMAP", "SMTP", "Diagnostics"]
    readonly property var currentTabs: isGmail ? gmailTabs : genericTabs

    Kirigami.Page {
        anchors.fill: parent
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        background: Item {}

        Rectangle {
            anchors.fill: parent
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── Title bar (consistent with AccountWizardDialog) ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: "transparent"

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeAllCursor
                        onPressed: root.startSystemMove()
                    }

                    QQC2.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: i18n("Accounts")
                        font.pixelSize: 14
                    }

                    AppLayout.TitleBarIconButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        buttonWidth: Kirigami.Units.gridUnit + 16
                        buttonHeight: Kirigami.Units.gridUnit + 5
                        iconSize: Kirigami.Units.gridUnit - 4
                        highlightColor: systemPalette.highlight
                        iconName: "window-close-symbolic"
                        onClicked: root.visible = false
                    }
                }

                // ── Toolbar ──────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: "transparent"

                    // Save & Close pinned left
                    AppLayout.MailActionButton {
                        anchors.left: parent.left
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "document-save"
                        text: i18n("Save & Close")
                        alwaysHighlighted: true
                        onTriggered: root.saveAndClose()
                    }

                    // Centered action buttons
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Kirigami.Units.largeSpacing

                        AppLayout.MailActionButton {
                            iconName: "list-add"
                            text: i18n("Add account")
                            onTriggered: {
                                root.visible = false
                                if (root.accountSetupObj)
                                    root.accountSetupObj.openAccountWizard()
                            }
                        }

                        // TODO: Wire "Set as default" to backend
                        AppLayout.MailActionButton {
                            iconName: "favorite"
                            text: i18n("Set as default")
                        }

                        // TODO: Wire "Save As" to backend
                        AppLayout.MailActionButton {
                            iconName: "document-save-as"
                            text: i18n("Save As")
                        }

                        AppLayout.MailActionButton {
                            iconName: "edit-delete"
                            text: i18n("Remove")
                            onTriggered: root.confirmRemoveAccount()
                        }

                        // TODO: Wire account reordering to backend
                        AppLayout.TitleBarIconButton {
                            buttonWidth: 28; buttonHeight: 28; iconSize: 16
                            highlightColor: systemPalette.highlight
                            iconName: "go-up-symbolic"
                        }
                        AppLayout.TitleBarIconButton {
                            buttonWidth: 28; buttonHeight: 28; iconSize: 16
                            highlightColor: systemPalette.highlight
                            iconName: "go-down-symbolic"
                        }
                    }
                }

                // ── Separator ────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.4)
                }

                // ── Main content: sidebar + tabs ─────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    AccountConfigSidebar {
                        Layout.preferredWidth: 240
                        Layout.fillHeight: true
                        accountRepositoryObj: root.accountRepositoryObj
                        accountManagerObj: root.accountManagerObj
                        selectedEmail: root.selectedEmail
                        onAccountSelected: (email) => root.loadAccount(email)
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: Qt.darker(Kirigami.Theme.backgroundColor, 1.4)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 0

                        // Tab bar with underline style
                        Row {
                            id: tabBar
                            Layout.fillWidth: false
                            Layout.leftMargin: Kirigami.Units.largeSpacing
                            spacing: Kirigami.Units.largeSpacing * 2

                            property int currentIndex: 0

                            Repeater {
                                model: root.currentTabs
                                delegate: Item {
                                    width: tabLabel.implicitWidth + 8
                                    height: tabLabel.implicitHeight + 12

                                    required property int index
                                    required property string modelData

                                    QQC2.Label {
                                        id: tabLabel
                                        anchors.centerIn: parent
                                        text: modelData
                                        opacity: tabBar.currentIndex === parent.index ? 1.0 : 0.6
                                        font.bold: tabBar.currentIndex === parent.index
                                    }

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        height: 2
                                        color: tabBar.currentIndex === parent.index
                                            ? systemPalette.highlight : "transparent"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: tabBar.currentIndex = parent.index
                                    }
                                }
                            }
                        }

                        // Content loader — avoids StackLayout bleed-through
                        Loader {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            sourceComponent: {
                                var tab = root.currentTabs[tabBar.currentIndex] || "General"
                                if (tab === "General") return generalComp
                                if (tab === "IMAP") return imapComp
                                if (tab === "SMTP") return smtpComp
                                if (tab === "Google Calendar") return googleCalComp
                                if (tab === "Diagnostics") return diagnosticsComp
                                return generalComp
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: generalComp
        AccountConfigGeneral {
            configData: root.editedConfig
            isGmail: root.isGmail
            accountManagerObj: root.accountManagerObj
            onConfigChanged: (key, value) => root.updateConfig(key, value)
        }
    }

    Component {
        id: imapComp
        AccountConfigImap {
            configData: root.editedConfig
            isGmail: root.isGmail
            onConfigChanged: (key, value) => root.updateConfig(key, value)
        }
    }

    Component {
        id: smtpComp
        AccountConfigSmtp {
            configData: root.editedConfig
            isGmail: root.isGmail
            onConfigChanged: (key, value) => root.updateConfig(key, value)
        }
    }

    Component {
        id: googleCalComp
        AccountConfigGoogleCalendar {}
    }

    Component {
        id: diagnosticsComp
        AccountConfigDiagnostics {
            configData: root.editedConfig
            isGmail: root.isGmail
            accountManagerObj: root.accountManagerObj
        }
    }
}
