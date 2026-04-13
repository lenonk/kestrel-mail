import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../Common"

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
    readonly property bool shown: !dismissedByAction && (targetHovered || bridgeHovered || contentHovered)

    visible: shown && titleText.length > 0

    onTargetHoveredChanged: {
        if (targetHovered) {
            dismissedByAction = false
            _enteredContent = false
        }
    }

    implicitWidth: 280
    width: implicitWidth

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

            StyledButton {
                id: primaryBtn
                visible: root.primaryButtonText.length > 0
                text: root.primaryButtonText
                icon.name: "mail-message"
                iconSize: 14
                iconLabelSpacing: 4
                leftPadding: 8
                rightPadding: 8
                topPadding: 3
                bottomPadding: 3
                onClicked: {
                    root.dismissedByAction = true
                    root.primaryTriggered()
                }
            }

            StyledButton {
                id: secondaryBtn
                visible: root.secondaryButtonText.length > 0
                text: root.secondaryButtonText
                icon.name: "im-user"
                iconSize: 14
                iconLabelSpacing: 4
                leftPadding: 8
                rightPadding: 8
                topPadding: 3
                bottomPadding: 3
                onClicked: {
                    root.dismissedByAction = true
                    root.secondaryTriggered()
                }
            }
        }
    }
}
