import QtQuick
import org.kde.kirigami as Kirigami
import ".."

Item {
    id: root
    width: Kirigami.Units.iconSizes.medium + 4
    height: Kirigami.Units.iconSizes.medium + 4

    property var appRoot
    property string mailbox: ""
    property string accountEmail: ""
    property string displayName: ""
    property string fallbackText: ""

    readonly property var avatarSources: appRoot
                                ? appRoot.senderAvatarSources(
                                   mailbox,
                                   "",
                                   "",
                                   accountEmail)
                                : []

    AvatarBadge {
        anchors.fill: parent
        size: root.width
        displayName: root.displayName
        fallbackText: root.fallbackText
        avatarSources: root.avatarSources
    }
}
