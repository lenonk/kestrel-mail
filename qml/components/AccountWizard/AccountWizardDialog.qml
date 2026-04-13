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
        oauthFlowStarted = false
        openSection = "automatic"
        accountSetupObj.discoverProvider()
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

        // Step 2 has encryption sub-pages (create → save → share)
        if (wizardStep === 2 && step2.hasSubPages && !step2.atLastSubPage) {
            step2.advanceSubPage()
            return
        }

        if (wizardStep < 3) {
            if (wizardStep === 2) step2.reset()
            wizardStep += 1
            return
        }

        if (!accountSetupObj) return

        // Always (re)start OAuth on Finish for OAuth providers.
        const supportsOAuth = !!(accountSetupObj.selectedProvider && accountSetupObj.selectedProvider.supportsOAuth2)
        if (supportsOAuth) {
            // Be defensive: provider discovery can be stale/empty after UI navigation.
            if (!accountSetupObj.selectedProvider || !accountSetupObj.selectedProvider.id) {
                accountSetupObj.discoverProvider()
            }

            root.oauthFlowStarted = true
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

    SystemPalette { id: systemPalette }

    Connections {
        target: accountSetupObj
        ignoreUnknownSignals: true
        function onOauthReadyChanged() {
            if (!accountSetupObj || !accountSetupObj.oauthReady) return
            if (!root.oauthFlowStarted) return

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

    OAuthBrowserWindow {
        id: oauthBrowserWindow
        oauthUrl: root.oauthBrowserUrl
        parentWindow: root
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

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

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
                                    model: [
                                        { n: 1, text: i18n("Account details") },
                                        { n: 2, text: i18n("Encryption") },
                                        { n: 3, text: i18n("Finish") }
                                    ]
                                    delegate: Row {
                                        spacing: 8
                                        Rectangle {
                                            width: 24
                                            height: 24
                                            radius: 12
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

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: root.wizardStep - 1

                            AccountWizardStep1 {
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
                                onOpenSectionChangeRequested: function(section) { root.openSection = section }
                                onSelectedProviderIdChangeRequested: function(providerId) { root.selectedProviderId = providerId }
                                onAccountNameDraftChangeRequested: function(text) { root.accountNameDraft = text }
                            }

                            AccountWizardStep2 { id: step2; accountSetupObj: root.accountSetupObj }

                            AccountWizardStep3 {}
                        }
                    }
                }

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
                        enabled: root.wizardStep > 1 || root.setupStarted || (root.wizardStep === 2 && step2.subPage > 0)
                        onClicked: {
                            // Try retreating within step 2 sub-pages first
                            if (root.wizardStep === 2 && step2.retreatSubPage()) {
                                return
                            }
                            if (root.wizardStep > 1) {
                                if (root.wizardStep === 2) step2.reset()
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
