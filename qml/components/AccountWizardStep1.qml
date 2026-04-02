import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: step1Root

    required property var accountSetupObj
    required property bool setupStarted
    required property string openSection
    required property string selectedProviderId
    required property string accountNameDraft
    required property bool hasStatus
    required property bool statusIsError
    required property int wizardStep

    signal beginAutoSetupRequested()
    signal advanceWizardRequested()
    signal openSectionChangeRequested(string section)
    signal selectedProviderIdChangeRequested(string providerId)
    signal accountNameDraftChangeRequested(string text)

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
        return step1Root.selectedProviderId === providerId
                ? Qt.lighter(systemPalette.highlight, 1.06)
                : Qt.darker(Kirigami.Theme.backgroundColor, 1.04)
    }

    SystemPalette { id: systemPalette }

    ColumnLayout {
        visible: !step1Root.setupStarted
        anchors.left: parent.left
        anchors.right: parent.right
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
            MouseArea { anchors.fill: parent; onClicked: step1Root.openSectionChangeRequested("automatic") }
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
                    visible: step1Root.openSection === "automatic"
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
                            visible: step1Root.hasStatus && step1Root.statusIsError
                            Row {
                                anchors.fill: parent
                                spacing: 10
                                Kirigami.Icon { source: "dialog-error"; width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter; color: "#bf3354" }
                                QQC2.Label { anchors.verticalCenter: parent.verticalCenter; text: step1Root.accountSetupObj ? step1Root.accountSetupObj.statusMessage : ""; color: "#bf3354"; elide: Text.ElideRight; width: parent.width - 26 }
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
                                text: step1Root.accountSetupObj ? step1Root.accountSetupObj.email : ""
                                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
                                onTextEdited: if (step1Root.accountSetupObj) step1Root.accountSetupObj.email = text
                                onAccepted: {
                                    if (emailField.text.trim().length > 3) step1Root.beginAutoSetupRequested()
                                }
                            }
                            QQC2.Button {
                                id: startBtn
                                text: step1Root.statusIsError ? i18n("Try Again") : i18n("Start")
                                Layout.preferredHeight: emailField.implicitHeight
                                Layout.preferredWidth: Kirigami.Units.gridUnit + 50
                                Layout.alignment: Qt.AlignVCenter
                                enabled: true
                                onClicked: {
                                    if (emailField.text.trim().length > 3) step1Root.beginAutoSetupRequested()
                                }

                                background: Rectangle {
                                    radius: 4
                                    readonly property color baseColor: step1Root.statusIsError ? "#c24a5a" : "#2fae62"
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
                    MouseArea { anchors.fill: parent; onClicked: step1Root.openSectionChangeRequested("mail") }
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
                    visible: step1Root.openSection === "mail"
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
                                onClicked: step1Root.selectedProviderIdChangeRequested(modelData)
                                background: Rectangle { radius: 4; color: step1Root.providerButtonColor(modelData); border.width: 1; border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2) }
                                contentItem: Column {
                                    spacing: 8
                                    anchors.centerIn: parent
                                    Kirigami.Icon { source: step1Root.providerIcon(modelData); width: 30; height: 30; anchors.horizontalCenter: parent.horizontalCenter }
                                    QQC2.Label { text: step1Root.providerDisplayName(modelData); anchors.horizontalCenter: parent.horizontalCenter }
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
                    MouseArea { anchors.fill: parent; onClicked: step1Root.openSectionChangeRequested("chat") }
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
                    visible: step1Root.openSection === "chat"
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
                    MouseArea { anchors.fill: parent; onClicked: step1Root.openSectionChangeRequested("calendar") }
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
                    visible: step1Root.openSection === "calendar"
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
                    MouseArea { anchors.fill: parent; onClicked: step1Root.openSectionChangeRequested("contacts") }
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
                    visible: step1Root.openSection === "contacts"
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
        visible: step1Root.setupStarted
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
            text: step1Root.accountNameDraft
            onTextEdited: step1Root.accountNameDraftChangeRequested(text)
            onAccepted: step1Root.advanceWizardRequested()
        }

        Item { Layout.fillHeight: true }
    }
}
