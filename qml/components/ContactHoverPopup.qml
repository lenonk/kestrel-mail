import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ArrowPopup {
    id: root

    property bool targetHovered: false
    property string titleText: ""
    property string subtitleText: ""
    property string avatarText: ""
    property var avatarSources: []
    property int avatarSize: Kirigami.Units.iconSizes.large + 8
    property int nameFontPixelSize: Kirigami.Theme.defaultFont.pixelSize - 1
    property int emailFontPixelSize: Kirigami.Theme.defaultFont.pixelSize - 2
    property string primaryButtonText: ""
    property string secondaryButtonText: ""

    signal primaryTriggered()
    signal secondaryTriggered()

    property bool dismissedByAction: false
    readonly property bool shown: !dismissedByAction && (targetHovered || hoverProbe.hovered)

    visible: shown && titleText.length > 0

    onTargetHoveredChanged: {
        if (targetHovered) dismissedByAction = false
    }

    implicitWidth: 280
    width: implicitWidth

    HoverHandler {
        id: hoverProbe
        margin: Math.max(0, root.anchorGap)
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6

        RowLayout {
            spacing: 6

            AvatarBadge {
                Layout.preferredWidth: root.avatarSize
                Layout.preferredHeight: root.avatarSize
                size: root.avatarSize
                displayName: root.titleText
                avatarSources: root.avatarSources
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                QQC2.Label {
                    text: root.titleText
                    font.bold: true
                    font.pixelSize: root.nameFontPixelSize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    text: root.subtitleText
                    font.pixelSize: root.emailFontPixelSize
                    opacity: 0.85
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        RowLayout {
            spacing: 6

            QQC2.Button {
                id: primaryBtn
                visible: root.primaryButtonText.length > 0
                text: root.primaryButtonText
                icon.name: "mail-message"
                flat: true
                leftPadding: 8
                rightPadding: 8
                topPadding: 3
                bottomPadding: 3
                onClicked: {
                    root.dismissedByAction = true
                    root.primaryTriggered()
                }
                contentItem: RowLayout {
                    spacing: 4
                    Kirigami.Icon {
                        source: primaryBtn.icon.name
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                    }
                    QQC2.Label {
                        text: primaryBtn.text
                        color: Kirigami.Theme.textColor
                    }
                }
                background: Rectangle {
                    color: primaryBtn.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                                          : (primaryBtn.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22)
                                                                : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                    border.width: 1
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                    radius: 0
                }
            }

            QQC2.Button {
                id: secondaryBtn
                visible: root.secondaryButtonText.length > 0
                text: root.secondaryButtonText
                icon.name: "im-user"
                flat: true
                leftPadding: 8
                rightPadding: 8
                topPadding: 3
                bottomPadding: 3
                onClicked: {
                    root.dismissedByAction = true
                    root.secondaryTriggered()
                }
                contentItem: RowLayout {
                    spacing: 4
                    Kirigami.Icon {
                        source: secondaryBtn.icon.name
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                    }
                    QQC2.Label {
                        text: secondaryBtn.text
                        color: Kirigami.Theme.textColor
                    }
                }
                background: Rectangle {
                    color: secondaryBtn.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                                            : (secondaryBtn.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22)
                                                                    : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                    border.width: 1
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                    radius: 0
                }
            }
        }
    }
}
