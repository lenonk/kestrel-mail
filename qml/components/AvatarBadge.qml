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

    property int avatarSourceIndex: 0
    property int avatarRetryNonce: 0
    readonly property string currentAvatarSource: {
        const base = (avatarSources && avatarSources.length > avatarSourceIndex)
                ? (avatarSources[avatarSourceIndex] || "")
                : ""
        if (!base.length || avatarRetryNonce <= 0) return base
        if (!(base.startsWith("https://") || base.startsWith("http://"))) return base
        const joiner = base.indexOf("?") >= 0 ? "&" : "?"
        return base + joiner + "kavatar_retry=" + avatarRetryNonce
    }

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    function computedInitials() {
        const rawName = (displayName || "").trim()
        const rawFallback = (fallbackText || "").trim()
        const raw = rawName.length ? rawName : rawFallback
        if (!raw.length)
            return "?"
        const parts = raw.split(/\s+/).filter(function(p) { return p.length > 0 })
        if (parts.length >= 2)
            return (parts[0][0] + parts[1][0]).toUpperCase()
        return raw[0].toUpperCase()
    }

    function stableHash(input) {
        let h = 2166136261
        const s = (input || "")
        for (let i = 0; i < s.length; ++i) {
            h ^= s.charCodeAt(i)
            h = Math.imul(h, 16777619)
        }
        return (h >>> 0)
    }

    function computedFallbackColor() {
        const key = ((displayName || "") + "|" + (fallbackText || "")).trim().toLowerCase()
        const hash = stableHash(key.length ? key : "unknown")
        const hue = (hash % 360) / 360.0
        return Qt.hsla(hue, 0.50, 0.45, 1.0)
    }

    onAvatarSourcesChanged: {
        avatarSourceIndex = 0
        avatarRetryNonce = 0
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

        onStatusChanged: {
            if (status === Image.Error) {
                // console.log("[avatar-image] error", source)
                if (root.avatarRetryNonce === 0) {
                    root.avatarRetryNonce = Date.now()
                    return
                }
                if (root.avatarSources && root.avatarSourceIndex < root.avatarSources.length - 1) {
                    root.avatarSourceIndex += 1
                    root.avatarRetryNonce = 0
                }
            }
        }
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
        // Scale from theme font, anchored to Kirigami icon sizing.
        // medium avatar ~= default font; larger/smaller avatars track proportionally.
        readonly property real _sizeScale: root.size / Kirigami.Units.iconSizes.medium
        font.pixelSize: Math.max(Kirigami.Theme.defaultFont.pixelSize,
                                 Math.round(Kirigami.Theme.defaultFont.pixelSize * _sizeScale))
    }
}
