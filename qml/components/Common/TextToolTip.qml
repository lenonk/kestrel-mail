import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Popup {
    id: root

    property string toolTipText: ""
    property real preferredX: 0
    property real preferredY: 0
    property bool clampToOverlay: true
    property real edgeMargin: 6
    property real delay: 0
    readonly property Item overlayItem: QQC2.Overlay.overlay

    modal: false
    focus: false
    closePolicy: QQC2.Popup.NoAutoClose
    padding: 0
    visible: false

    x: {
        if (!clampToOverlay || !overlayItem)
            return preferredX
        const minX = edgeMargin
        const maxX = Math.max(minX, overlayItem.width - width - edgeMargin)
        return Math.max(minX, Math.min(preferredX, maxX))
    }

    y: {
        if (!clampToOverlay || !overlayItem)
            return preferredY
        const minY = edgeMargin
        const maxY = Math.max(minY, overlayItem.height - height - edgeMargin)
        return Math.max(minY, Math.min(preferredY, maxY))
    }

    background: Rectangle {
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
        border.color: Kirigami.Theme.disabledTextColor
    }

    contentItem: QQC2.Label {
        text: root.toolTipText
        leftPadding: 8
        rightPadding: 8
        topPadding: 4
        bottomPadding: 4
    }

    Timer {
        id: timer
        interval: root.delay      // milliseconds
        running: false
        repeat: false      // true = keeps firing, false = fires once
        onTriggered: {
            root.visible = true
        }
    }

    function show() {
        if (!visible && toolTipText.length > 0)
            timer.start();
    }

    function hide() {
        if (timer.running) timer.stop();
        visible = false
    }
}
