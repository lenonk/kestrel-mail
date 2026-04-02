import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root

    property var avatarSources: []
    property string displayName: ""
    property string fallbackText: ""
    property int size: Kirigami.Units.iconSizes.large

    readonly property string currentAvatarSource: (avatarSources && avatarSources.length > 0)
                                                   ? (avatarSources[0] || "")
                                                   : ""

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    function computedInitials() {
        return (typeof dataStore !== "undefined" && dataStore)
            ? dataStore.avatarInitials(displayName || "", fallbackText || "") : "?"
    }

    function computedFallbackColor() {
        return (typeof dataStore !== "undefined" && dataStore)
            ? dataStore.avatarColor(displayName || "", fallbackText || "") : "#666"
    }

    Rectangle {
        id: avatarRect
        anchors.fill: parent
        radius: width / 2
        color: (avatarRaw.status === Image.Ready ? "transparent" : computedFallbackColor())
    }

    Image {
        id: avatarRaw
        anchors.fill: parent
        source: root.currentAvatarSource
        fillMode: Image.PreserveAspectCrop
        smooth: true
        visible: false
        asynchronous: true
    }

    OpacityMask {
        anchors.fill: parent
        source: avatarRaw
        maskSource: Rectangle { width: root.size; height: root.size; radius: width / 2 }
        visible: avatarRaw.status === Image.Ready
    }

    QQC2.Label {
        anchors.centerIn: parent
        visible: avatarRaw.status !== Image.Ready
        text: root.computedInitials()
        color: Kirigami.Theme.textColor
        font.bold: true
        // Scale from theme font, anchored to Kirigami icon sizing.
        // medium avatar ~= default font; larger/smaller avatars track proportionally.
        readonly property real _sizeScale: root.size / Kirigami.Units.iconSizes.medium
        font.pixelSize: Math.max(10,
                                 Math.max(Kirigami.Theme.defaultFont.pixelSize,
                                          Math.round(Kirigami.Theme.defaultFont.pixelSize * _sizeScale)))
    }
}
