import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

QQC2.ApplicationWindow {
    width: 1100
    height: 760
    visible: true
    title: "Kestrel IMAP Lab"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            QQC2.Label { text: "Account:" }
            QQC2.ComboBox {
                id: accountBox
                Layout.preferredWidth: 360
                model: imapLab.accounts
                textRole: "email"
                onActivated: {
                    const item = model[index]
                    if (item && item.email)
                        imapLab.selectedAccountEmail = item.email
                }
            }

            QQC2.Label { text: "Template:" }
            QQC2.ComboBox {
                id: templateBox
                Layout.fillWidth: true
                model: imapLab.commandTemplates
                textRole: "name"
                onActivated: imapLab.applyTemplate(index)
            }

            QQC2.Button {
                text: imapLab.running ? "Running..." : "Run"
                enabled: !imapLab.running
                onClicked: imapLab.runCurrentCommand()
            }
        }

        QQC2.TextArea {
            id: commandEdit
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            wrapMode: TextEdit.WrapAnywhere
            placeholderText: "Type raw IMAP command here (tag optional). Example: UID SEARCH ALL"
            text: imapLab.commandText
            onTextChanged: if (focus) imapLab.commandText = text
        }

        RowLayout {
            Layout.fillWidth: true
            QQC2.CheckBox {
                text: "Append output"
                checked: imapLab.appendOutput
                onToggled: imapLab.appendOutput = checked
            }
            QQC2.Label { text: "Elapsed: " + imapLab.elapsedMs + " ms" }
            Item { Layout.fillWidth: true }
            QQC2.Button { text: "Refresh Accounts"; onClicked: imapLab.refreshAccounts() }
        }

        QQC2.ScrollView {
            id: outputScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AlwaysOn

            QQC2.TextArea {
                id: outputArea
                width: outputScroll.availableWidth
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: imapLab.output
                font.family: "monospace"

                onTextChanged: {
                    cursorPosition = length
                    Qt.callLater(() => {
                        outputScroll.ScrollBar.vertical.position = 1.0
                    })
                }

                QQC2.Menu {
                    id: outputMenu
                    QQC2.MenuItem {
                        text: "Copy all"
                        onTriggered: {
                            outputArea.selectAll()
                            outputArea.copy()
                            outputArea.deselect()
                        }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: (eventPoint, button) => {
                        if (button !== Qt.RightButton)
                            return
                        outputMenu.x = eventPoint.position.x
                        outputMenu.y = eventPoint.position.y
                        outputMenu.open()
                    }
                }
            }
        }
    }
}
