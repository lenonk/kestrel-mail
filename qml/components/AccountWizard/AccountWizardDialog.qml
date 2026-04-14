import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import "../Common"
import "../Layout"

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
    property bool oauthFlowStarted: false
    property double lastWizardAdvanceAt: 0

    // Flow type: "oauth" (3 steps) or "manual" (7 steps).
    property string flowType: "oauth"
    readonly property int maxStep: flowType === "manual" ? 7 : 3

    readonly property var oauthSteps: [
        { n: 1, text: i18n("Account details") },
        { n: 2, text: i18n("Encryption") },
        { n: 3, text: i18n("Finish") }
    ]
    readonly property var manualSteps: [
        { n: 1, text: i18n("Identity") },
        { n: 2, text: i18n("Incoming server") },
        { n: 3, text: i18n("Outgoing server") },
        { n: 4, text: i18n("Test configuration") },
        { n: 5, text: i18n("Account details") },
        { n: 6, text: i18n("Encryption") },
        { n: 7, text: i18n("Finish") }
    ]
    readonly property var wizardSteps: flowType === "manual" ? manualSteps : oauthSteps

    signal toastRequested(string message, bool isError)

    readonly property bool hasStatus: accountSetupObj && accountSetupObj.statusMessage.length > 0
    readonly property bool statusIsError: hasStatus && (accountSetupObj.statusMessage.toLowerCase().indexOf("failed") >= 0
                                                  || accountSetupObj.statusMessage.toLowerCase().indexOf("cannot") >= 0
                                                  || accountSetupObj.statusMessage.toLowerCase().indexOf("missing") >= 0)

    // Map (flowType, wizardStep) → StackLayout index.
    // Manual: 0=Identity 1=Incoming 2=Outgoing 3=Test 4=AccountDetails 5=Encryption 6=Finish
    // OAuth:  7=AccountDetails 8=Encryption 9=Finish
    function pageIndex() {
        if (flowType === "manual") return wizardStep - 1
        return 6 + wizardStep  // 7,8,9
    }

    // Encryption step number depends on flow.
    readonly property int encryptionStep: flowType === "manual" ? 6 : 2

    function open() {
        visible = true
        raise()
        requestActivate()
    }

    function beginAutoSetup() {
        if (!accountSetupObj) return
        setupStarted = true
        oauthFlowStarted = false
        openSection = "automatic"

        // Start async discovery.
        if (typeof providerProfiles !== "undefined" && providerProfiles.discoverForEmailAsync)
            providerProfiles.discoverForEmailAsync(accountSetupObj.email)
        else
            accountSetupObj.discoverProvider()

        var em = (accountSetupObj.email || "").trim()
        accountNameDraft = em.length > 0 ? em : i18n("My Account")
        wizardStep = 1
    }

    function beginManualSetup() {
        if (!accountSetupObj) return
        setupStarted = true
        oauthFlowStarted = false
        flowType = "manual"
        accountSetupObj.applyDiscoveryResult({
            "id": "generic", "displayName": "Generic IMAP/SMTP",
            "imapHost": "", "imapPort": 993,
            "smtpHost": "", "smtpPort": 587,
            "supportsOAuth2": false, "flowType": "manual"
        })
        var em = (accountSetupObj.email || "").trim()
        accountNameDraft = em.length > 0 ? em : i18n("My Account")
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

        // Encryption sub-pages.
        if (wizardStep === root.encryptionStep) {
            const encStep = flowType === "manual" ? manualStep2Enc : oauthStep2Enc
            if (encStep.hasSubPages && !encStep.atLastSubPage) {
                encStep.advanceSubPage()
                return
            }
        }

        if (wizardStep < root.maxStep) {
            if (wizardStep === root.encryptionStep) {
                const encStep = flowType === "manual" ? manualStep2Enc : oauthStep2Enc
                encStep.reset()
            }
            wizardStep += 1
            return
        }

        // Finish step.
        if (!accountSetupObj) return

        if (flowType === "oauth") {
            const supportsOAuth = !!(accountSetupObj.selectedProvider && accountSetupObj.selectedProvider.supportsOAuth2)
            if (supportsOAuth) {
                if (!accountSetupObj.selectedProvider || !accountSetupObj.selectedProvider.id)
                    accountSetupObj.discoverProvider()

                root.oauthFlowStarted = true
                accountSetupObj.beginOAuth()
                toastRequested(i18n("Starting OAuth..."), false)
                var launchUrl = accountSetupObj.oauthUrl ? accountSetupObj.oauthUrl.toString() : ""

                if (launchUrl.length === 0) {
                    accountSetupObj.discoverProvider()
                    accountSetupObj.beginOAuth()
                    launchUrl = accountSetupObj.oauthUrl ? accountSetupObj.oauthUrl.toString() : ""
                }

                if (launchUrl.length > 0) {
                    root.oauthBrowserUrl = launchUrl
                    const providerId = (accountSetupObj.selectedProvider && accountSetupObj.selectedProvider.id)
                                       ? accountSetupObj.selectedProvider.id.toString().toLowerCase() : ""
                    if (providerId === "gmail") {
                        toastRequested(i18n("Opening OAuth in browser..."), false)
                        Qt.openUrlExternally(root.oauthBrowserUrl)
                    } else {
                        oauthBrowserWindow.show()
                        oauthBrowserWindow.raise()
                        oauthBrowserWindow.requestActivate()
                    }
                } else {
                    toastRequested(i18n("OAuth did not start. %1", accountSetupObj.statusMessage || ""), true)
                }
                return
            }
        }

        // Manual or non-OAuth finish: save directly.
        if (accountSetupObj.saveCurrentAccount(root.accountNameDraft, "Auto")) {
            toastRequested(i18n("Account saved successfully."), false)
            root.close()
        } else {
            toastRequested(i18n("Save failed: %1", accountSetupObj.statusMessage || ""), true)
        }
    }

    SystemPalette { id: systemPalette }

    // ── Discovery result handler ──────────────────────────────────────────
    Connections {
        target: typeof providerProfiles !== "undefined" ? providerProfiles : null
        ignoreUnknownSignals: true
        function onDiscoveryFinished(result) {
            if (!accountSetupObj) return
            accountSetupObj.applyDiscoveryResult(result)
            root.flowType = (result.flowType || "oauth").toString()
        }
    }

    Connections {
        target: accountSetupObj
        ignoreUnknownSignals: true
        function onOauthReadyChanged() {
            if (!accountSetupObj || !accountSetupObj.oauthReady) return
            if (!root.oauthFlowStarted) return

            toastRequested(i18n("OAuth complete. Saving account..."), false)

            if (oauthBrowserWindow.visible)
                oauthBrowserWindow.close()

            if (accountSetupObj.saveCurrentAccount(root.accountNameDraft, "Auto")) {
                toastRequested(i18n("Account connected successfully."), false)
                root.close()
            } else {
                toastRequested(i18n("OAuth succeeded, but account save failed: %1", accountSetupObj.statusMessage), true)
            }
        }
    }

    OAuthBrowserWindow {
        id: oauthBrowserWindow
        oauthUrl: root.oauthBrowserUrl
        parentWindow: root
    }

    Kirigami.Page {
        anchors.fill: parent
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        background: Item {}

        Rectangle {
            anchors.fill: parent
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                // ── Title bar ─────────────────────────────────────────
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

                    TitleBarIconButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        buttonWidth: Kirigami.Units.gridUnit + 16
                        buttonHeight: Kirigami.Units.gridUnit + 5
                        iconSize: Kirigami.Units.gridUnit - 4
                        highlightColor: systemPalette.highlight
                        iconName: "window-close-symbolic"
                        onClicked: root.close()
                    }
                }

                // ── Body ──────────────────────────────────────────────
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        // ── Sidebar ───────────────────────────────────
                        Rectangle {
                            Layout.preferredWidth: root.setupStarted ? 160 : 0
                            Layout.fillHeight: true
                            visible: root.setupStarted
                            color: "transparent"

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.top: parent.top
                                anchors.topMargin: 24
                                spacing: 8

                                Repeater {
                                    model: root.wizardSteps
                                    delegate: Row {
                                        spacing: 8
                                        Rectangle {
                                            width: 24; height: 24; radius: 12
                                            color: "transparent"
                                            border.width: 1.5
                                            border.color: modelData.n <= root.wizardStep ? Qt.lighter(Kirigami.Theme.textColor, 1.0) : Qt.lighter(Kirigami.Theme.textColor, 0.5)
                                            QQC2.Label { anchors.centerIn: parent; text: modelData.n; font.pixelSize: 11; opacity: modelData.n <= root.wizardStep ? 1 : 0.55 }
                                        }
                                        QQC2.Label { anchors.verticalCenter: parent.verticalCenter; text: modelData.text; font.pixelSize: 13; opacity: modelData.n <= root.wizardStep ? 1 : 0.55 }
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

                        // ── Pages ─────────────────────────────────────
                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: root.pageIndex()

                            // Manual flow pages (indices 0-6)
                            ManualIdentityStep   { accountSetupObj: root.accountSetupObj }
                            ManualIncomingStep   { accountSetupObj: root.accountSetupObj }
                            ManualOutgoingStep   { accountSetupObj: root.accountSetupObj }
                            ManualTestStep       { accountSetupObj: root.accountSetupObj }
                            AccountWizardStep1   {
                                accountSetupObj: root.accountSetupObj
                                setupStarted: root.setupStarted
                                openSection: root.openSection
                                selectedProviderId: root.selectedProviderId
                                accountNameDraft: root.accountNameDraft
                                hasStatus: root.hasStatus
                                statusIsError: root.statusIsError
                                wizardStep: root.wizardStep
                                onBeginAutoSetupRequested: root.beginAutoSetup()
                                onAdvanceWizardRequested: root.advanceWizard()
                                onBeginManualSetupRequested: root.beginManualSetup()
                                onOpenSectionChangeRequested: function(section) { root.openSection = section }
                                onSelectedProviderIdChangeRequested: function(providerId) { root.selectedProviderId = providerId }
                                onAccountNameDraftChangeRequested: function(text) { root.accountNameDraft = text }
                            }
                            AccountWizardStep2   { id: manualStep2Enc; accountSetupObj: root.accountSetupObj }
                            AccountWizardStep3   { providerId: root.selectedProviderId; accountSetupObj: root.accountSetupObj }

                            // OAuth flow pages (indices 7-9)
                            AccountWizardStep1   {
                                accountSetupObj: root.accountSetupObj
                                setupStarted: root.setupStarted
                                openSection: root.openSection
                                selectedProviderId: root.selectedProviderId
                                accountNameDraft: root.accountNameDraft
                                hasStatus: root.hasStatus
                                statusIsError: root.statusIsError
                                wizardStep: root.wizardStep
                                onBeginAutoSetupRequested: root.beginAutoSetup()
                                onAdvanceWizardRequested: root.advanceWizard()
                                onBeginManualSetupRequested: root.beginManualSetup()
                                onOpenSectionChangeRequested: function(section) { root.openSection = section }
                                onSelectedProviderIdChangeRequested: function(providerId) { root.selectedProviderId = providerId }
                                onAccountNameDraftChangeRequested: function(text) { root.accountNameDraft = text }
                            }
                            AccountWizardStep2   { id: oauthStep2Enc; accountSetupObj: root.accountSetupObj }
                            AccountWizardStep3   { providerId: root.selectedProviderId; accountSetupObj: root.accountSetupObj }
                        }
                    }
                }

                // ── Footer ────────────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.bottomMargin: 20

                    QQC2.Label {
                        text: root.hasStatus ? (accountSetupObj ? accountSetupObj.statusMessage : "") : ""
                        visible: root.hasStatus
                        color: root.statusIsError ? KestrelColors.errorRed : Qt.lighter(Kirigami.Theme.textColor, 1.0)
                        elide: Text.ElideRight
                        Layout.maximumWidth: 460
                    }

                    Item { Layout.fillWidth: true }

                    QQC2.Button {
                        id: backButton
                        text: i18n("Back")
                        enabled: root.wizardStep > 1 || root.setupStarted
                        onClicked: {
                            // Try retreating within encryption sub-pages first.
                            if (root.wizardStep === root.encryptionStep) {
                                const encStep = root.flowType === "manual" ? manualStep2Enc : oauthStep2Enc
                                if (encStep.retreatSubPage()) return
                            }
                            if (root.wizardStep > 1) {
                                if (root.wizardStep === root.encryptionStep) {
                                    const encStep = root.flowType === "manual" ? manualStep2Enc : oauthStep2Enc
                                    encStep.reset()
                                }
                                root.wizardStep = Math.max(1, root.wizardStep - 1)
                            } else {
                                root.setupStarted = false
                                root.flowType = "oauth"
                                root.openSection = "automatic"
                            }
                        }
                    }
                    QQC2.Button {
                        id: nextBtn
                        Layout.preferredWidth: backButton.width - 4
                        Layout.preferredHeight: backButton.height - 4
                        text: root.wizardStep >= root.maxStep ? i18n("Finish") : i18n("Next")
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
                        text: i18n("Cancel")
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
