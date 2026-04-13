import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import "../Layout"

Window {
    id: root
    width: 520
    height: 440
    visible: false
    title: i18n("Change account avatar")
    modality: Qt.ApplicationModal
    color: "transparent"
    flags: Qt.Dialog | Qt.FramelessWindowHint

    property int selectedIndex: 0
    property string selectedCustomPath: ""
    property string chosenAvatar: ""

    signal avatarChosen(string avatarPath)

    readonly property var avatarFiles: {
        var list = []
        for (var i = 1; i <= 18; i++) {
            var num = i < 10 ? "0" + i : "" + i
            list.push("qrc:/qml/images/account-avatars/avatar-" + num + ".svg")
        }
        return list
    }

    function open() {
        selectedCustomPath = ""
        visible = true
        raise()
        requestActivate()
    }

    SystemPalette { id: systemPalette }

    FileDialog {
        id: browseDialog
        title: i18n("Choose Avatar Image")
        nameFilters: [i18n("Image Files (*.png *.jpg *.jpeg *.svg *.webp *.bmp)"), i18n("All Files (*)")]
        onAccepted: {
            root.selectedCustomPath = browseDialog.selectedFile.toString()
            root.selectedIndex = -1
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 0
            spacing: 0

            // ── Title bar ─────────────────────────────────────────────
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
                    text: i18n("Change account avatar")
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

            // ── Content ───────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8

                QQC2.Label { text: i18n("Choose account avatar"); font.pixelSize: 16; font.bold: true }
                QQC2.Label { text: i18n("Select one of the predefined avatars from the list or add your own"); opacity: 0.7 }

                Item { Layout.preferredHeight: 4 }

                // ── Grid of avatars ───────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.24)
                    radius: 4

                    GridView {
                        id: avatarGrid
                        anchors.fill: parent
                        anchors.margins: 8
                        cellWidth: 76
                        cellHeight: 76
                        clip: true
                        model: root.avatarFiles

                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: avatarGrid.cellWidth - 4
                            height: avatarGrid.cellHeight - 4
                            radius: 4
                            color: root.selectedIndex === index && root.selectedCustomPath.length === 0
                                   ? systemPalette.highlight : "transparent"

                            Image {
                                anchors.centerIn: parent
                                width: 40
                                height: 40
                                source: modelData
                                sourceSize: Qt.size(40, 40)
                                fillMode: Image.PreserveAspectFit
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.selectedIndex = index
                                    root.selectedCustomPath = ""
                                }
                            }
                        }
                    }
                }

                // ── Browse link ───────────────────────────────────────
                RowLayout {
                    spacing: 6
                    Kirigami.Icon { source: "folder-open"; Layout.preferredWidth: 16; Layout.preferredHeight: 16; opacity: 0.7 }
                    QQC2.Label {
                        text: root.selectedCustomPath.length > 0
                              ? i18n("Browse...") + "  " + root.selectedCustomPath.split("/").pop()
                              : i18n("Browse...")
                        color: Qt.lighter(systemPalette.highlight, 1.2)
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: browseDialog.open() }
                    }
                }
            }

            // ── Buttons ───────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: 20
                Layout.bottomMargin: 16
                Layout.topMargin: 8

                Item { Layout.fillWidth: true }

                QQC2.Button {
                    id: okBtn
                    text: i18n("Ok")
                    Layout.preferredWidth: cancelBtn.width
                    Layout.preferredHeight: cancelBtn.height
                    onClicked: {
                        if (root.selectedCustomPath.length > 0) {
                            root.chosenAvatar = root.selectedCustomPath
                        } else if (root.selectedIndex >= 0 && root.selectedIndex < root.avatarFiles.length) {
                            root.chosenAvatar = root.avatarFiles[root.selectedIndex]
                        }
                        root.avatarChosen(root.chosenAvatar)
                        root.close()
                    }

                    background: Rectangle {
                        radius: 4
                        color: okBtn.down ? Qt.darker(systemPalette.highlight, 1.22)
                              : (okBtn.hovered ? Qt.darker(systemPalette.highlight, 1.12) : systemPalette.highlight)
                    }
                    contentItem: QQC2.Label {
                        text: okBtn.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: "white"
                        font.bold: true
                    }
                }
                QQC2.Button {
                    id: cancelBtn
                    text: i18n("Cancel")
                    onClicked: root.close()
                }
            }
        }

        // ── Border ────────────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
            z: 9999
        }
    }
}
