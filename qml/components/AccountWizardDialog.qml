import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtWebEngine
import org.kde.kirigami as Kirigami

Window {
    id: root
    width: 840
    height: 650
    visible: false
    title: i18n("New Account")
    modality: Qt.ApplicationModal
    color: "transparent"
    flags: Qt.Dialog | Qt.FramelessWindowHint

    property var accountSetupObj: null
    property var accountRepositoryObj: null
    property string selectedProviderId: "gmail"
    property int wizardStep: 1
    property bool setupStarted: false
    property string openSection: "automatic"
    property string accountNameDraft: ""
    property string oauthBrowserUrl: ""
    property bool oauthUseMobileView: true
    property double lastWizardAdvanceAt: 0

    signal toastRequested(string message, bool isError)

    readonly property bool hasStatus: accountSetupObj && accountSetupObj.statusMessage.length > 0
    readonly property bool statusIsError: hasStatus && (accountSetupObj.statusMessage.toLowerCase().indexOf("failed") >= 0
                                                  || accountSetupObj.statusMessage.toLowerCase().indexOf("cannot") >= 0
                                                  || accountSetupObj.statusMessage.toLowerCase().indexOf("missing") >= 0)

    function open() {
        visible = true
        raise()
        requestActivate()
    }

    function beginAutoSetup() {
        if (!accountSetupObj) return
        setupStarted = true
        openSection = "automatic"
        accountSetupObj.discoverProvider()
        var em = (accountSetupObj.email || "").trim()
        var local = em.indexOf("@") > 0 ? em.slice(0, em.indexOf("@")) : em
        accountNameDraft = local.length > 0 ? local : i18n("My Account")
        wizardStep = 1
    }

    function advanceWizard() {
        const nowMs = Date.now()
        if (nowMs - lastWizardAdvanceAt < 250) return
        lastWizardAdvanceAt = nowMs

        if (wizardStep === 1 && !setupStarted) {
            if (accountSetupObj && (accountSetupObj.email || "").trim().length > 3) {
                beginAutoSetup()
            }
            return
        }

        if (wizardStep < 3) {
            wizardStep += 1
            return
        }

        if (!accountSetupObj) return

        // OAuth starts only when Finish is pressed.
        if (!accountSetupObj.oauthReady) {
            // Be defensive: provider discovery can be stale/empty after UI navigation.
            if (!accountSetupObj.selectedProvider || !accountSetupObj.selectedProvider.id) {
                accountSetupObj.discoverProvider()
            }

            accountSetupObj.beginOAuth()
            toastRequested(i18n("Finish pressed. Starting OAuth..."), false)
            var launchUrl = accountSetupObj.oauthUrl ? accountSetupObj.oauthUrl.toString() : ""

            // Retry once after rediscovery if OAuth URL did not populate.
            if (launchUrl.length === 0) {
                toastRequested(i18n("OAuth URL missing. Retrying provider discovery..."), true)
                accountSetupObj.discoverProvider()
                accountSetupObj.beginOAuth()
                launchUrl = accountSetupObj.oauthUrl ? accountSetupObj.oauthUrl.toString() : ""
            }

            if (launchUrl.length > 0) {
                root.oauthBrowserUrl = launchUrl
                const providerId = (accountSetupObj.selectedProvider && accountSetupObj.selectedProvider.id)
                                   ? accountSetupObj.selectedProvider.id.toString().toLowerCase()
                                   : ""
                if (providerId === "gmail") {
                    toastRequested(i18n("Opening OAuth in browser..."), false)
                    Qt.openUrlExternally(root.oauthBrowserUrl)
                } else {
                    toastRequested(i18n("Opening OAuth window..."), false)
                    oauthBrowserWindow.show()
                    oauthBrowserWindow.raise()
                    oauthBrowserWindow.requestActivate()
                }
            } else {
                toastRequested(i18n("OAuth did not start. %1", accountSetupObj ? accountSetupObj.statusMessage : i18n("No status available.")), true)
            }
            return
        }

        if (accountSetupObj.saveCurrentAccount(root.accountNameDraft, "Auto")) {
            root.close()
        } else {
            root.wizardStep = 1
            root.openSection = "automatic"
        }
    }

    function providerIcon(providerId) {
        if (providerId === "gmail") return "mail-message"
        if (providerId === "outlook") return "internet-mail"
        if (providerId === "yahoo") return "mail-mark-unread"
        if (providerId === "aol") return "globe"
        if (providerId === "exchange") return "network-server"
        return "overflow-menu-horizontal"
    }

    function providerDisplayName(providerId) {
        if (providerId === "gmail") return "Gmail"
        if (providerId === "outlook") return "Outlook.com"
        if (providerId === "yahoo") return "Yahoo!"
        if (providerId === "aol") return "Aol."
        if (providerId === "exchange") return "Exchange"
        return "Other"
    }

    function providerButtonColor(providerId) {
        return selectedProviderId === providerId
                ? Qt.lighter(systemPalette.highlight, 1.06)
                : Qt.darker(Kirigami.Theme.backgroundColor, 1.04)
    }

    SystemPalette { id: systemPalette }

    Connections {
        target: accountSetupObj
        ignoreUnknownSignals: true
        function onOauthReadyChanged() {
            if (!accountSetupObj || !accountSetupObj.oauthReady) return

            toastRequested(i18n("OAuth complete. Saving account..."), false)

            if (oauthBrowserWindow.visible) {
                oauthBrowserWindow.close()
            }

            // Auto-finish flow after successful OAuth.
            if (accountSetupObj.saveCurrentAccount(root.accountNameDraft, "Auto")) {
                toastRequested(i18n("Account connected successfully."), false)
                root.close()
            } else {
                toastRequested(i18n("OAuth succeeded, but account save failed: %1", accountSetupObj.statusMessage), true)
            }
        }
    }

    Window {
        id: oauthBrowserWindow
        title: i18n("Google Sign-in")
        modality: Qt.ApplicationModal
        transientParent: root
        visible: false
        width: Math.min(Math.max(980, root.width + 80), Screen.desktopAvailableWidth - 120)
        height: Math.min(Math.max(720, root.height + 80), Screen.desktopAvailableHeight - 120)
        color: Qt.darker(Kirigami.Theme.backgroundColor, 1.06)

        onClosing: oauthWebView.stop()

        WebEngineView {
            id: oauthWebView
            anchors.fill: parent
            url: root.oauthBrowserUrl
            zoomFactor: 1.0
            settings.localContentCanAccessRemoteUrls: true
            settings.autoLoadImages: true
            settings.javascriptCanOpenWindows: true
            settings.errorPageEnabled: true
        }
    }

    Kirigami.Page {
        anchors.fill: parent
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        background: Item {}

        Rectangle {
            anchors.fill: parent
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)
            border.width: 0
            border.color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 10

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
                        text: i18n("New Account")
                        font.pixelSize: 14
                    }

                    QQC2.Button {
                        id: closeBtn
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 32
                        implicitHeight: 32
                        leftPadding: 0
                        rightPadding: 0
                        background: Rectangle {
                            radius: 4
                            color: closeBtn.down ? Qt.darker(systemPalette.highlight, 1.45)
                                                 : (closeBtn.hovered ? Qt.darker(systemPalette.highlight, 1.7) : "transparent")
                        }
                        onClicked: root.close()
                        contentItem: Kirigami.Icon {
                            source: "window-close-symbolic"
                            width: 16
                            height: 16
                            color: Kirigami.Theme.textColor
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.preferredWidth: root.setupStarted ? 210 : 0
                            Layout.fillHeight: true
                            visible: root.setupStarted
                            color: "transparent"

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.top: parent.top
                                anchors.topMargin: 24
                                spacing: 4

                                Repeater {
                                    model: [
                                        { n: 1, text: i18n("Account details") },
                                        { n: 2, text: i18n("Encryption") },
                                        { n: 3, text: i18n("Finish") }
                                    ]
                                    delegate: Row {
                                        spacing: 10
                                        Rectangle {
                                            width: 42
                                            height: 42
                                            radius: 21
                                            color: "transparent"
                                            border.width: 2
                                            border.color: modelData.n <= root.wizardStep ? Qt.lighter(Kirigami.Theme.textColor, 1.0) : Qt.lighter(Kirigami.Theme.textColor, 0.5)
                                            QQC2.Label { anchors.centerIn: parent; text: modelData.n; font.bold: true; opacity: modelData.n <= root.wizardStep ? 1 : 0.55 }
                                        }
                                        QQC2.Label { anchors.verticalCenter: parent.verticalCenter; text: modelData.text; font.pixelSize: 19; opacity: modelData.n <= root.wizardStep ? 1 : 0.55 }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: root.setupStarted ? 1 : 0
                            Layout.fillHeight: true
                            visible: root.setupStarted
                            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                            opacity: 0.7
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: root.wizardStep - 1

                            Item {
                                ColumnLayout {
                                    visible: !root.setupStarted
                                    anchors.left : parent.left
                                    anchors.right : parent.right
                                    anchors.leftMargin: 26
                                    anchors.rightMargin: 26
                                    spacing: 10

                                    QQC2.Label { text: i18n("Welcome! Set up an account"); font.pixelSize: 18; font.bold: true }

                                    Item { Layout.preferredHeight: 7 }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 52
                                        radius: 4
                                        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
                                        border.width: 1
                                        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                                        MouseArea { anchors.fill: parent; onClicked: root.openSection = "automatic" }
                                        RowLayout {
                                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                                            Kirigami.Icon { source: "view-refresh"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; opacity: 0.8 }
                                            QQC2.Label { text: i18n("Automatic Setup"); font.bold: true }
                                            Item { Layout.fillWidth: true }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 580

                                        ColumnLayout {
                                            anchors.fill: parent
                                            spacing: 10

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 150
                                                visible: root.openSection === "automatic"
                                                clip: true

                                                ColumnLayout {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 20
                                                    spacing: 8
                                                    QQC2.Label { text: i18n("Enter your email and press Start.") }
                                                    Item { Layout.preferredHeight: 6 }
                                                    Rectangle {
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: 28
                                                        color: "transparent"
                                                        visible: root.hasStatus && root.statusIsError
                                                        Row {
                                                            anchors.fill: parent
                                                            spacing: 10
                                                            Kirigami.Icon { source: "dialog-error"; width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter; color: "#bf3354" }
                                                            QQC2.Label { anchors.verticalCenter: parent.verticalCenter; text: accountSetupObj ? accountSetupObj.statusMessage : ""; color: "#bf3354"; elide: Text.ElideRight; width: parent.width - 26 }
                                                        }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 12
                                                        QQC2.Label { text: i18n("Email:") }
                                                        QQC2.TextField {
                                                            id: emailField
                                                            Layout.preferredWidth: 380
                                                            placeholderText: i18n("you@example.com")
                                                            text: accountSetupObj ? accountSetupObj.email : ""
                                                            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
                                                            onTextEdited: if (accountSetupObj) accountSetupObj.email = text
                                                            onAccepted: {
                                                                if (emailField.text.trim().length > 3) root.beginAutoSetup()
                                                            }
                                                        }
                                                        QQC2.Button {
                                                            id: startBtn
                                                            text: root.statusIsError ? i18n("Try Again") : i18n("Start")
                                                            Layout.preferredHeight: emailField.implicitHeight
                                                            Layout.preferredWidth: Kirigami.Units.gridUnit + 50
                                                            Layout.alignment: Qt.AlignVCenter
                                                            enabled: true
                                                            onClicked: {
                                                                if (emailField.text.trim().length > 3) root.beginAutoSetup()
                                                            }

                                                            background: Rectangle {
                                                                radius: 4
                                                                readonly property color baseColor: root.statusIsError ? "#c24a5a" : "#2fae62"
                                                                color: !startBtn.enabled ? Qt.darker(Kirigami.Theme.backgroundColor, 1.1)
                                                                      : (startBtn.down ? Qt.darker(baseColor, 1.18)
                                                                                       : (startBtn.hovered ? Qt.darker(baseColor, 1.08) : baseColor))
                                                            }

                                                            contentItem: QQC2.Label {
                                                                text: startBtn.text
                                                                horizontalAlignment: Text.AlignHCenter
                                                                verticalAlignment: Text.AlignVCenter
                                                                color: startBtn.enabled ? "white" : Qt.lighter(Kirigami.Theme.disabledTextColor, 0.9)
                                                                font.bold: true
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 52
                                                radius: 4
                                                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
                                                border.width: 1
                                                border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                                                MouseArea { anchors.fill: parent; onClicked: root.openSection = "mail" }
                                                RowLayout {
                                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                                                    Kirigami.Icon { source: "mail-message"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; opacity: 0.8 }
                                                    QQC2.Label { text: i18n("Mail"); font.bold: true }
                                                    Item { Layout.fillWidth: true }
                                                }
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 150
                                                visible: root.openSection === "mail"
                                                clip: true

                                                RowLayout {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 20
                                                    spacing: 12
                                                    Repeater {
                                                        model: ["gmail", "outlook", "yahoo", "aol", "exchange", "other"]
                                                        delegate: QQC2.Button {
                                                            Layout.preferredWidth: 86
                                                            Layout.preferredHeight: 96
                                                            flat: true
                                                            onClicked: root.selectedProviderId = modelData
                                                            background: Rectangle { radius: 4; color: root.providerButtonColor(modelData); border.width: 1; border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2) }
                                                            contentItem: Column {
                                                                spacing: 8
                                                                anchors.centerIn: parent
                                                                Kirigami.Icon { source: root.providerIcon(modelData); width: 30; height: 30; anchors.horizontalCenter: parent.horizontalCenter }
                                                                QQC2.Label { text: root.providerDisplayName(modelData); anchors.horizontalCenter: parent.horizontalCenter }
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 52
                                                radius: 4
                                                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
                                                border.width: 1
                                                border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                                                MouseArea { anchors.fill: parent; onClicked: root.openSection = "chat" }
                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 12
                                                    anchors.rightMargin: 12
                                                    spacing: 10
                                                    Kirigami.Icon { source: "im-user"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; opacity: 0.8 }
                                                    QQC2.Label { text: i18n("Chat / Group Chat"); font.bold: true }
                                                    Item { Layout.fillWidth: true }
                                                }
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 150
                                                visible: root.openSection === "chat"
                                                clip: true

                                                ColumnLayout {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 20
                                                    spacing: 6

                                                    QQC2.Label { text: i18n("Chat accounts preview"); opacity: 0.9 }
                                                    RowLayout {
                                                        spacing: 8
                                                        Rectangle { width: 12; height: 12; radius: 6; color: "#4CAF50" }
                                                        QQC2.Label { text: i18n("Signal: Connected") }
                                                    }
                                                    RowLayout {
                                                        spacing: 8
                                                        Rectangle { width: 12; height: 12; radius: 6; color: "#FFC107" }
                                                        QQC2.Label { text: i18n("Matrix: Needs sign-in") }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 52
                                                radius: 4
                                                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
                                                border.width: 1
                                                border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                                                MouseArea { anchors.fill: parent; onClicked: root.openSection = "calendar" }
                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 12
                                                    anchors.rightMargin: 12
                                                    spacing: 10
                                                    Kirigami.Icon { source: "view-calendar"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; opacity: 0.8 }
                                                    QQC2.Label { text: i18n("Calendar"); font.bold: true }
                                                    Item { Layout.fillWidth: true }
                                                }
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 150
                                                visible: root.openSection === "calendar"
                                                clip: true

                                                ColumnLayout {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 20
                                                    spacing: 6

                                                    QQC2.Label { text: i18n("Upcoming events preview"); opacity: 0.9 }
                                                    QQC2.Label { text: i18n("• Architecture sync — Today 10:30") ; opacity: 0.85 }
                                                    QQC2.Label { text: i18n("• Team retro — Tomorrow 14:00") ; opacity: 0.85 }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 52
                                                radius: 4
                                                color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
                                                border.width: 1
                                                border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                                                MouseArea { anchors.fill: parent; onClicked: root.openSection = "contacts" }
                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 12
                                                    anchors.rightMargin: 12
                                                    spacing: 10
                                                    Kirigami.Icon { source: "user-identity"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; opacity: 0.8 }
                                                    QQC2.Label { text: i18n("Contacts"); font.bold: true }
                                                    Item { Layout.fillWidth: true }
                                                }
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 150
                                                visible: root.openSection === "contacts"
                                                clip: true

                                                ColumnLayout {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 20
                                                    spacing: 6

                                                    QQC2.Label { text: i18n("Contacts preview"); opacity: 0.9 }
                                                    QQC2.Label { text: i18n("• Lenon Kitchens") ; opacity: 0.85 }
                                                    QQC2.Label { text: i18n("• Kestrel Team") ; opacity: 0.85 }
                                                }
                                            }
                                            Item { Layout.fillHeight: true }
                                        }
                                    }
                                }

                                ColumnLayout {
                                    visible: root.setupStarted
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 26
                                    anchors.rightMargin: 26
                                    anchors.top: parent.top
                                    spacing: 10

                                    onVisibleChanged: {
                                        if (visible) Qt.callLater(function() { accountNameField.forceActiveFocus() })
                                    }

                                    QQC2.Label { text: i18n("Account details"); font.pixelSize: 18; font.bold: true }

                                    Item { Layout.preferredHeight: 7 }

                                    QQC2.Label { text: i18n("Account name:") }
                                    QQC2.TextField {
                                        id: accountNameField
                                        Layout.fillWidth: true
                                        placeholderText: i18n("Personal Gmail")
                                        text: root.accountNameDraft
                                        onTextEdited: root.accountNameDraft = text
                                        onAccepted: root.advanceWizard()
                                    }

                                    Item { Layout.fillHeight: true }
                                }
                            }

                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 26
                                    anchors.rightMargin: 12
                                    anchors.topMargin: 16
                                    spacing: 14
                                    QQC2.Label { text: i18n("Set up additional encryption (E2EE)"); font.pixelSize: 22; font.bold: true }
                                    QQC2.Label { text: i18n("Protect your communication and data with an extra layer of security via PGP end-to-end encryption technology."); wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.95 }
                                    QQC2.Label { text: i18n("Learn more"); color: Qt.lighter(systemPalette.highlight, 1.2) }
                                    Repeater {
                                        model: [
                                            { title: i18n("Create encryption keypair"), subtitle: i18n("I want to protect my email privacy with PGP encryption."), icon: "document-new" },
                                            { title: i18n("Import existing PGP keypair"), subtitle: i18n("I already have a keypair for this account and want to import it."), icon: "document-import" },
                                            { title: i18n("Continue without PGP encryption"), subtitle: i18n("I don't want to encrypt my emails for now."), icon: "object-unlocked", selected: true }
                                        ]
                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 108
                                            radius: 4
                                            color: modelData.selected ? Qt.lighter(systemPalette.highlight, 1.2) : "transparent"
                                            border.width: 1
                                            border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.24)
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 16
                                                spacing: 16
                                                Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: 38; Layout.preferredHeight: 38; opacity: 0.78 }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    QQC2.Label { text: modelData.title; font.bold: true; font.pixelSize: 16 }
                                                    QQC2.Label { text: modelData.subtitle; wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.9 }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 26
                                    anchors.topMargin: 16
                                    QQC2.Label { text: i18n("Finish") ; font.pixelSize: 22; font.bold: true }
                                    QQC2.Label { text: i18n("Review complete. Press Finish to connect and save this account.") }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.rightMargin: 20
                    Layout.bottomMargin: 20

                    QQC2.Label {
                        text: root.hasStatus ? (accountSetupObj ? accountSetupObj.statusMessage : "") : ""
                        visible: root.hasStatus
                        color: root.statusIsError ? "#bf3354" : Qt.lighter(Kirigami.Theme.textColor, 1.0)
                        elide: Text.ElideRight
                        Layout.maximumWidth: 460
                    }

                    Item { Layout.fillWidth: true }

                    QQC2.Button {
                        id: backButton
                        text: i18n("Back")
                        enabled: root.wizardStep > 1 || root.setupStarted
                        onClicked: {
                            if (root.wizardStep > 1) {
                                root.wizardStep = Math.max(1, root.wizardStep - 1)
                            } else {
                                root.setupStarted = false
                                root.openSection = "automatic"
                            }
                        }
                    }
                    QQC2.Button {
                        id: nextBtn
                        Layout.preferredWidth: backButton.width - 4
                        Layout.preferredHeight: backButton.height - 4
                        text: root.wizardStep >= 3 ? i18n("Finish") : i18n("Next")
                        enabled: true
                        onClicked: root.advanceWizard()

                        background: Rectangle {
                            radius: 4
                            color: !nextBtn.enabled ? Qt.darker(Kirigami.Theme.backgroundColor, 1.1)
                                  : (nextBtn.down ? Qt.darker(systemPalette.highlight, 1.22)
                                                  : (nextBtn.hovered ? Qt.darker(systemPalette.highlight, 1.12) : systemPalette.highlight))
                        }

                        contentItem: QQC2.Label {
                            text: nextBtn.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: nextBtn.enabled ? "white" : Qt.lighter(Kirigami.Theme.disabledTextColor, 0.9)
                            font.bold: true
                        }
                    }
                    QQC2.Button {
                        id: cancelButton
                        text: i18n("Cancel");
                        onClicked: root.close()
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
            z: 9999
        }
    }
}
