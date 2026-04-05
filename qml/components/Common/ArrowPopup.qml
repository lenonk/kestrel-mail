import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    property Item anchorItem: null
    property real edgeMargin: 6
    property real anchorGap: Kirigami.Units.smallSpacing
    property real arrowHeight: 8
    property real arrowWidth: 18
    property real arrowLeftPx: 70
    property real contentPadding: 10
    property color borderColor: Qt.lighter(Kirigami.Theme.backgroundColor, 1.35)

    default property alias popupContent: contentContainer.data

    // Gap-bridge hover: covers from the trigger's bottom edge through the
    // arrow to the content box so the cursor can traverse the gap without
    // the popup vanishing.  Disabled once the mouse enters the content area
    // (so leaving the popup upward through the gap closes it immediately).
    property bool _enteredContent: false
    readonly property bool bridgeHovered: _gapBridgeHover.hovered
    readonly property bool contentHovered: _contentAreaHover.hovered

    readonly property point anchorPos: visible && anchorItem
        ? anchorItem.mapToItem(QQC2.Overlay.overlay, 0, 0)
        : Qt.point(0, 0)
    readonly property real anchorCenterX: anchorPos.x + (anchorItem ? anchorItem.width / 2 : 0)

    parent: QQC2.Overlay.overlay
    z: 2000

    x: {
        const arrowCenterFromLeft = arrowLeftPx + (arrowWidth / 2)
        const desired = anchorCenterX - arrowCenterFromLeft
        const minX = edgeMargin
        const maxX = Math.max(minX, QQC2.Overlay.overlay.width - width - edgeMargin)
        return Math.max(minX, Math.min(desired, maxX))
    }
    y: anchorPos.y + (anchorItem ? anchorItem.height : 0) + anchorGap

    implicitHeight: contentContainer.implicitHeight + 2 * contentPadding + arrowHeight
    height: implicitHeight

    onWidthChanged: backgroundCanvas.requestPaint()
    onHeightChanged: backgroundCanvas.requestPaint()
    onVisibleChanged: if (visible) backgroundCanvas.requestPaint()
    onArrowLeftPxChanged: backgroundCanvas.requestPaint()
    onArrowWidthChanged: backgroundCanvas.requestPaint()
    onArrowHeightChanged: backgroundCanvas.requestPaint()

    Item {
        id: _gapBridge
        x: 0
        y: -root.anchorGap
        width: root.width
        height: root.anchorGap + root.arrowHeight + root.contentPadding + 2
        z: 2

        HoverHandler {
            id: _gapBridgeHover
            enabled: !root._enteredContent
        }
    }

    Canvas {
        id: backgroundCanvas
        anchors.fill: parent

        function cssColor(c) {
            if (c === undefined || c === null)
                return "#202020"
            if (typeof c === "string")
                return c
            if (c.r !== undefined && c.g !== undefined && c.b !== undefined) {
                const a = (c.a !== undefined) ? c.a : 1
                return "rgba(" + Math.round(c.r * 255) + ","
                       + Math.round(c.g * 255) + ","
                       + Math.round(c.b * 255) + "," + a + ")"
            }
            return "" + c
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            const ax = Math.max(8, Math.min(root.arrowLeftPx, width - root.arrowWidth - 8))

            ctx.beginPath()
            ctx.moveTo(ax + root.arrowWidth / 2, 0)
            ctx.lineTo(ax, root.arrowHeight)
            ctx.lineTo(0, root.arrowHeight)
            ctx.lineTo(0, height)
            ctx.lineTo(width, height)
            ctx.lineTo(width, root.arrowHeight)
            ctx.lineTo(ax + root.arrowWidth, root.arrowHeight)
            ctx.closePath()
            ctx.fillStyle = cssColor(Kirigami.Theme.backgroundColor)
            ctx.fill()

            ctx.beginPath()
            ctx.moveTo(ax + root.arrowWidth / 2, 0)
            ctx.lineTo(ax, root.arrowHeight)
            ctx.lineTo(0, root.arrowHeight)
            ctx.lineTo(0, height)
            ctx.lineTo(width, height)
            ctx.lineTo(width, root.arrowHeight)
            ctx.lineTo(ax + root.arrowWidth, root.arrowHeight)
            ctx.closePath()
            ctx.lineWidth = 1
            ctx.strokeStyle = cssColor(root.borderColor)
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
    }

    Item {
        id: contentContainer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.contentPadding
        anchors.rightMargin: root.contentPadding
        anchors.topMargin: root.arrowHeight + root.contentPadding
        implicitHeight: childrenRect.height
        z: 1

        HoverHandler {
            id: _contentAreaHover
            onHoveredChanged: {
                if (hovered) root._enteredContent = true
            }
        }
    }
}
