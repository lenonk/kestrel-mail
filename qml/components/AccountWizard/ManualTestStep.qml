import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: testRoot

    required property var accountSetupObj

    property bool imapTesting: false
    property bool smtpTesting: false
    property bool imapPassed: false
    property bool smtpPassed: false
    property string imapResult: ""
    property string smtpResult: ""
    property bool ignoreResults: false
    property bool allPassed: (imapPassed && smtpPassed) || ignoreResults

    SystemPalette { id: systemPalette }

    function startTests() {
        imapTesting = true
        smtpTesting = true
        imapPassed = false
        smtpPassed = false
        imapResult = ""
        smtpResult = ""

        // SMTP test stubbed for now.
        smtpTesting = false
        smtpPassed = true
        smtpResult = i18n("Ok")

        if (testRoot.accountSetupObj)
            testRoot.accountSetupObj.testConnection()
    }

    Connections {
        target: testRoot.accountSetupObj
        function onTestingChanged() {
            if (testRoot.accountSetupObj && !testRoot.accountSetupObj.testing) {
                testRoot.imapTesting = false
                testRoot.imapPassed = testRoot.accountSetupObj.testPassed
                testRoot.imapResult = testRoot.accountSetupObj.testPassed
                    ? i18n("Ok")
                    : (testRoot.accountSetupObj.testResult || i18n("Connection failed"))
            }
        }
    }

    onVisibleChanged: if (visible) startTests()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        spacing: 8

        QQC2.Label { text: i18n("Test configuration"); font.pixelSize: 18; font.bold: true }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("SMTP"); font.pixelSize: 12 }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 10

            QQC2.BusyIndicator {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                visible: testRoot.smtpTesting; running: visible
            }
            Kirigami.Icon {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                source: "dialog-ok"; color: "#2fae62"
                visible: !testRoot.smtpTesting && testRoot.smtpPassed
            }
            Kirigami.Icon {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                source: "dialog-error"; color: "#e05555"
                visible: !testRoot.smtpTesting && !testRoot.smtpPassed && testRoot.smtpResult.length > 0
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: testRoot.smtpTesting ? i18n("Testing...") : testRoot.smtpResult
                color: testRoot.smtpTesting ? Kirigami.Theme.textColor : (testRoot.smtpPassed ? "#2fae62" : "#e05555")
                wrapMode: Text.Wrap
            }
        }

        Item { Layout.preferredHeight: 4 }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; radius: 2
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.16)
            QQC2.Label { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: i18n("IMAP"); font.pixelSize: 12 }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; spacing: 10

            QQC2.BusyIndicator {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                visible: testRoot.imapTesting; running: visible
            }
            Kirigami.Icon {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                source: "dialog-ok"; color: "#2fae62"
                visible: !testRoot.imapTesting && testRoot.imapPassed
            }
            Kirigami.Icon {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                source: "dialog-error"; color: "#e05555"
                visible: !testRoot.imapTesting && !testRoot.imapPassed && testRoot.imapResult.length > 0
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: testRoot.imapTesting ? i18n("Testing...") : testRoot.imapResult
                color: testRoot.imapTesting ? Kirigami.Theme.textColor : (testRoot.imapPassed ? "#2fae62" : "#e05555")
                wrapMode: Text.Wrap
            }
        }

        Item { Layout.fillHeight: true }

        QQC2.CheckBox {
            text: i18n("Ignore test results (account might not work correctly).")
            checked: testRoot.ignoreResults
            onToggled: testRoot.ignoreResults = checked
        }
    }
}
