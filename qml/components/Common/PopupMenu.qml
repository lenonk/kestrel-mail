import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import org.kde.kirigami as Kirigami

QQC2.Menu {
    id: root

    property real horizontalOffset: 0
    property real verticalOffset: 0
    property double lastClosedAtMs: -1
    property int reopenGuardMs: 120

    x: horizontalOffset
    y: (parent ? parent.height : 0) + verticalOffset

    onClosed: lastClosedAtMs = Date.now()

    function openIfClosed() {
        if (opened)
            return

        const now = Date.now()
        if (lastClosedAtMs >= 0 && (now - lastClosedAtMs) < reopenGuardMs)
            return

        open()
    }

    background: Item {
        implicitWidth: panel.implicitWidth
        implicitHeight: panel.implicitHeight

        Rectangle {
            id: panel
            anchors.fill: parent
            radius: 0
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
        }

        MultiEffect {
            anchors.fill: panel
            source: panel
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowBlur: 0.4
            shadowHorizontalOffset: 4
            shadowVerticalOffset: 4
            shadowColor: "black"
            opacity: 0.50
        }
    }
}
