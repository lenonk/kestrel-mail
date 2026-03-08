import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    property Item anchorItem: null
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
    property real edgeMargin: 6
    property real anchorGap: Kirigami.Units.smallSpacing
    property real arrowHeight: 8
    property real arrowWidth: 18
    property real arrowLeftPx: 70

    signal primaryTriggered()
    signal secondaryTriggered()

    parent: QQC2.Overlay.overlay
    z: 2000

    property point anchorPos: visible && anchorItem ? anchorItem.mapToItem(QQC2.Overlay.overlay, 0, 0) : Qt.point(0, 0)
    readonly property real anchorCenterX: anchorPos.x + (anchorItem ? anchorItem.width / 2 : 0)
    property bool dismissedByAction: false
    readonly property bool shown: !dismissedByAction && (targetHovered || hoverProbe.hovered)

    visible: shown && titleText.length > 0
    onVisibleChanged: if (visible) backgroundCanvas.requestPaint()

    onTargetHoveredChanged: {
        if (targetHovered) dismissedByAction = false
    }

    x: {
        const arrowCenterFromLeft = arrowLeftPx + (arrowWidth / 2)
        const desired = anchorCenterX - arrowCenterFromLeft
        const minX = edgeMargin
        const maxX = Math.max(minX, QQC2.Overlay.overlay.width - width - edgeMargin)
        return Math.max(minX, Math.min(desired, maxX))
    }
    y: anchorPos.y + (anchorItem ? anchorItem.height : 0) + anchorGap

    onWidthChanged: backgroundCanvas.requestPaint()
    onHeightChanged: backgroundCanvas.requestPaint()
    onArrowLeftPxChanged: backgroundCanvas.requestPaint()
    onArrowWidthChanged: backgroundCanvas.requestPaint()
    onArrowHeightChanged: backgroundCanvas.requestPaint()

    implicitWidth: 280
    implicitHeight: content.implicitHeight + 20 + arrowHeight
    width: implicitWidth
    height: implicitHeight

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
                return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + a + ")"
            }
            return "" + c
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            const ax = Math.max(8, Math.min(root.arrowLeftPx, width - root.arrowWidth - 8))

            // fill
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

            // border
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
            ctx.strokeStyle = cssColor(Qt.lighter(Kirigami.Theme.backgroundColor, 1.35))
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
    }

    // Hover bridge: extend hit area upward to close the gap between
    // triggering link hover area and popup body/arrow tip.
    HoverHandler {
        id: hoverProbe
        margin: Math.max(0, root.anchorGap)
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: arrowHeight + 10
        spacing: 6
        z: 1

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
