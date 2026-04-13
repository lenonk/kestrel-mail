import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Item {
    id: step2Root

    // Sub-page: 0 = choice, 1 = create keypair, 2 = save key, 3 = share key
    property int subPage: 0
    property int selectedIndex: 2
    property int saveSelectedIndex: 0
    property int shareSelectedIndex: 0
    property string pgpPassword: ""
    property string pgpPasswordConfirm: ""
    property int pgpKeySize: 2048
    property string pgpError: ""
    property var accountSetupObj: null

    // Expose to parent so Next/Back can drive sub-pages
    property bool hasSubPages: selectedIndex <= 1
    property bool atLastSubPage: (selectedIndex === 0 && subPage === 3)
                              || (selectedIndex === 1 && subPage === 3)
                              || selectedIndex === 2

    function advanceSubPage() {
        pgpError = ""
        if (selectedIndex === 0) {
            // Create flow: choice → create keypair → save key → share key
            if (subPage === 0) { subPage = 1; return true }
            if (subPage === 1) {
                // Validate and generate
                if (pgpPassword.length === 0) { pgpError = i18n("Please enter a password."); return true }
                if (pgpPassword !== pgpPasswordConfirm) { pgpError = i18n("Passwords do not match."); return true }
                var email = (accountSetupObj && accountSetupObj.email) ? accountSetupObj.email : ""
                pgpKeyManager.generateKeypair(email, pgpPassword, pgpKeySize)
                // Don't advance yet — wait for keypairGenerated signal
                return true
            }
            if (subPage === 2) {
                // Save page → open save dialog if "Save now" selected, otherwise skip
                if (saveSelectedIndex === 0) { saveDialog.open(); return true }
                subPage = 3; return true
            }
            if (subPage === 3) { return false } // done, let parent advance to step 3
            return false
        }
        if (selectedIndex === 1) {
            // Import flow: choice → file dialog → share
            if (subPage === 0) { importDialog.open(); return true }
            if (subPage === 3) { return false }
            if (subPage < 3) { subPage++; return true }
            return false
        }
        return false
    }

    function retreatSubPage() {
        pgpError = ""
        if (subPage > 0) {
            // Skip back over subPage 2 to 1 if we came from import flow
            if (selectedIndex === 1 && subPage === 3) { subPage = 0; return true }
            subPage--; return true
        }
        return false
    }

    function reset() { subPage = 0; pgpError = "" }

    SystemPalette { id: systemPalette }

    Connections {
        target: pgpKeyManager
        function onKeypairGenerated(ok, errorMessage) {
            if (ok) {
                step2Root.subPage = 2 // advance to save page
            } else {
                step2Root.pgpError = errorMessage
            }
        }
    }

    FileDialog {
        id: importDialog
        title: i18n("Import PGP Keypair")
        nameFilters: [i18n("PGP Key Files (*.asc *.gpg *.pgp *.key)"), i18n("All Files (*)")]
        onAccepted: {
            // TODO: actually import the key file
            step2Root.subPage = 3
        }
    }

    FileDialog {
        id: saveDialog
        title: i18n("Save Private Key")
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("PGP Key Files (*.pem)"), i18n("All Files (*)")]
        onAccepted: {
            pgpKeyManager.exportPrivateKey(saveDialog.selectedFile)
            step2Root.subPage = 3
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: step2Root.subPage

        // ── Page 0: Encryption choice ─────────────────────────────────────
        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                spacing: 8

                QQC2.Label { text: i18n("Set up additional encryption (E2EE)"); font.pixelSize: 18; font.bold: true }
                QQC2.Label {
                    text: i18n("Protect your communication and data with an extra layer of security via PGP end-to-end encryption technology.")
                    wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                }
                QQC2.Label {
                    text: i18n("Learn more"); color: Qt.lighter(systemPalette.highlight, 1.2)
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://kestrelmail.org/encryption") }
                }

                Item { Layout.preferredHeight: 4 }

                Repeater {
                    model: [
                        { title: i18n("Create encryption keypair"), subtitle: i18n("I want to protect my email privacy with PGP encryption."), icon: "document-encrypt" },
                        { title: i18n("Import existing PGP keypair"), subtitle: i18n("I already have a keypair for this account and want to import it."), icon: "document-import" },
                        { title: i18n("Continue without PGP encryption"), subtitle: i18n("I don't want to encrypt my emails for now."), icon: "object-unlocked" }
                    ]
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 64; radius: 4
                        color: step2Root.selectedIndex === index ? systemPalette.highlight : "transparent"
                        border.width: step2Root.selectedIndex === index ? 0 : 1
                        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.24)
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: step2Root.selectedIndex = index }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 14
                            Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: 28; Layout.preferredHeight: 28; opacity: 0.78 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                QQC2.Label { text: modelData.title; font.bold: true; font.pixelSize: 13 }
                                QQC2.Label { text: modelData.subtitle; wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; font.pixelSize: 12 }
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // ── Page 1: Create new PGP keypair ────────────────────────────────
        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                spacing: 8

                QQC2.Label { text: i18n("Create new PGP keypair"); font.pixelSize: 18; font.bold: true }
                QQC2.Label {
                    text: i18n("Create a PGP keypair for this account and assign a password to it. You will need the password to open messages encrypted with this keypair. Keep the password safe.")
                    wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                }
                QQC2.Label {
                    text: i18n("Learn more"); color: Qt.lighter(systemPalette.highlight, 1.2)
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://kestrelmail.org/encryption-create-keypair") }
                }

                Item { Layout.preferredHeight: 8 }

                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    QQC2.Label { text: i18n("Password:"); Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight }
                    QQC2.TextField {
                        id: passwordField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        text: step2Root.pgpPassword
                        onTextEdited: step2Root.pgpPassword = text
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    QQC2.Label { text: i18n("Confirm password:"); Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight }
                    QQC2.TextField {
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        text: step2Root.pgpPasswordConfirm
                        onTextEdited: step2Root.pgpPasswordConfirm = text
                    }
                }
                QQC2.Label {
                    text: i18n("Password should be different from your mail account password")
                    opacity: 0.45; font.pixelSize: 12; Layout.leftMargin: 134
                }

                Item { Layout.preferredHeight: 4 }

                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    QQC2.Label { text: i18n("Key size:"); Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight }
                    QQC2.ComboBox {
                        id: keySizeCombo
                        model: ["1024 bit", "2048 bit", "4096 bit"]
                        currentIndex: 1
                        onCurrentIndexChanged: {
                            const sizes = [1024, 2048, 4096]
                            step2Root.pgpKeySize = sizes[currentIndex]
                        }
                    }
                    QQC2.Label {
                        text: i18n("What's this?"); color: Qt.lighter(systemPalette.highlight, 1.2)
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://kestrelmail.org/encryption-create-keypair#picking-a-key-size") }
                    }
                }

                QQC2.Label {
                    visible: step2Root.pgpError.length > 0
                    text: step2Root.pgpError
                    color: "#e05555"
                    wrapMode: Text.Wrap; Layout.fillWidth: true
                }

                QQC2.BusyIndicator {
                    visible: pgpKeyManager.generating
                    running: pgpKeyManager.generating
                    Layout.alignment: Qt.AlignHCenter
                }
                QQC2.Label {
                    visible: pgpKeyManager.generating
                    text: i18n("Generating keypair...")
                    opacity: 0.7
                    Layout.alignment: Qt.AlignHCenter
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ── Page 2: Save your private key ─────────────────────────────────
        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                spacing: 8

                QQC2.Label { text: i18n("Save your private key"); font.pixelSize: 18; font.bold: true }
                QQC2.Label {
                    text: i18n("Save the private key to a safe storage. You can also use it across other applications and devices. If your private key is lost, you won't be able to read the encrypted messages anymore.")
                    wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                }
                QQC2.Label {
                    text: i18n("Learn more"); color: Qt.lighter(systemPalette.highlight, 1.2)
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://kestrelmail.org/encryption-save-key") }
                }

                Item { Layout.preferredHeight: 4 }

                Repeater {
                    model: [
                        { title: i18n("Save now"), subtitle: i18n("Save a copy of your private key to a local drive. (Important: if you lose the device, you lose the key. Multiple backups are recommended.)"), icon: "document-save" },
                        { title: i18n("Don't save"), subtitle: i18n("You risk losing access to the content of the encrypted emails."), icon: "dialog-cancel" }
                    ]
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 64; radius: 4
                        color: step2Root.saveSelectedIndex === index ? systemPalette.highlight : "transparent"
                        border.width: step2Root.saveSelectedIndex === index ? 0 : 1
                        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.24)
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: step2Root.saveSelectedIndex = index
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 14
                            Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: 28; Layout.preferredHeight: 28; opacity: 0.78 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                QQC2.Label { text: modelData.title; font.bold: true; font.pixelSize: 13 }
                                QQC2.Label { text: modelData.subtitle; wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; font.pixelSize: 12 }
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // ── Page 3: Share your public key ─────────────────────────────────
        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                spacing: 8

                QQC2.Label { text: i18n("Share your public key"); font.pixelSize: 18; font.bold: true }
                QQC2.Label {
                    text: i18n("Let others send you encrypted messages by sharing your public key. Anyone who wants to contact you securely can look up your public key and use it for encryption.")
                    wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                }
                QQC2.Label {
                    text: i18n("Learn more"); color: Qt.lighter(systemPalette.highlight, 1.2)
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://kestrelmail.org/encryption-share-key") }
                }

                Item { Layout.preferredHeight: 4 }

                Repeater {
                    model: [
                        { title: i18n("Share now"), subtitle: i18n("I want to share my public key to easily receive encrypted messages from anybody."), icon: "document-share" },
                        { title: i18n("Don't share"), subtitle: i18n("I want to take care of my public key distribution myself."), icon: "dialog-cancel" }
                    ]
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 64; radius: 4
                        color: step2Root.shareSelectedIndex === index ? systemPalette.highlight : "transparent"
                        border.width: step2Root.shareSelectedIndex === index ? 0 : 1
                        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.24)
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: step2Root.shareSelectedIndex = index }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 14
                            Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: 28; Layout.preferredHeight: 28; opacity: 0.78 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                QQC2.Label { text: modelData.title; font.bold: true; font.pixelSize: 13 }
                                QQC2.Label { text: modelData.subtitle; wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7; font.pixelSize: 12 }
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
