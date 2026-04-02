import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../Common"

ArrowPopup {
    id: root

    property var appRoot: null

    signal searchTriggered(string filterQuery)

    implicitWidth: 320
    width: implicitWidth
    contentPadding: 14

    // Close on click outside
    MouseArea {
        parent: QQC2.Overlay.overlay
        anchors.fill: parent
        visible: root.visible
        z: root.z - 1
        onClicked: root.visible = false
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        // Include Archive
        QQC2.CheckBox {
            id: includeArchive
            text: i18n("Include Archive")
            checked: false
        }

        // Has the words
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("Has the words:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: hasWordsField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // Doesn't have
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("Doesn't have:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: doesntHaveField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // From
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("From:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: fromField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // To
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("To:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: toField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // Subject
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("Subject:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: subjectField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // Has Attachment
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            QQC2.Label {
                text: i18n("Has Attachment:")
                font.pixelSize: 12
                opacity: 0.8
                Layout.fillWidth: true
            }
            QQC2.Switch {
                id: hasAttachmentSwitch
            }
        }

        // Date within
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            QQC2.Label {
                text: i18n("Date within:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.ComboBox {
                id: dateWithinCombo
                Layout.fillWidth: true
                model: [
                    i18n("Any time"),
                    i18n("Past week"),
                    i18n("Past month"),
                    i18n("Past 3 months"),
                    i18n("Past year")
                ]
                currentIndex: 0
                font.pixelSize: 12
            }
        }

        // Choose fields link (stub)
        QQC2.Label {
            text: "<a href='#'>" + i18n("Choose fields") + "</a>"
            font.pixelSize: 11
            opacity: 0.6
            Layout.alignment: Qt.AlignRight
            textFormat: Text.RichText
            // Stub — no action
        }

        // Bottom row: Create Search Folder... + Search button
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 8

            StyledButton {
                text: i18n("Create Search Folder...")
                enabled: false
                font.pixelSize: 12
                radius: 4
                leftPadding: 8
                rightPadding: 8
                topPadding: 4
                bottomPadding: 4
            }

            Item { Layout.fillWidth: true }

            QQC2.Button {
                id: searchBtn
                text: i18n("Search")
                font.pixelSize: 12
                leftPadding: 16
                rightPadding: 16
                topPadding: 5
                bottomPadding: 5
                icon.name: "edit-find"
                contentItem: RowLayout {
                    spacing: 4
                    Kirigami.Icon {
                        source: searchBtn.icon.name
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        color: "white"
                    }
                    QQC2.Label {
                        text: searchBtn.text
                        color: "white"
                        font.pixelSize: 12
                    }
                }
                background: Rectangle {
                    color: searchBtn.down ? Qt.darker(KestrelColors.searchOrange, 1.2)
                         : (searchBtn.hovered ? Qt.lighter(KestrelColors.searchOrange, 1.1)
                                              : KestrelColors.searchOrange)
                    radius: 4
                }
                onClicked: {
                    // Build query from the "Has the words" field for now
                    const q = hasWordsField.text.trim()
                    if (q.length > 0) {
                        root.visible = false
                        root.searchTriggered(q)
                    }
                }
            }
        }
    }
}
