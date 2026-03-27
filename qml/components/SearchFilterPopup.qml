import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    property Item anchorItem: null
    property var appRoot: null
    property real edgeMargin: 6
    property real anchorGap: Kirigami.Units.smallSpacing
    property real arrowHeight: 8
    property real arrowWidth: 18
    property real arrowLeftPx: 70

    signal searchTriggered(string filterQuery)

    parent: QQC2.Overlay.overlay
    z: 2000

    property point anchorPos: visible && anchorItem
        ? anchorItem.mapToItem(QQC2.Overlay.overlay, 0, 0)
        : Qt.point(0, 0)
    readonly property real anchorCenterX: anchorPos.x + (anchorItem ? anchorItem.width / 2 : 0)

    x: {
        const arrowCenterFromLeft = arrowLeftPx + (arrowWidth / 2)
        const desired = anchorCenterX - arrowCenterFromLeft
        const minX = edgeMargin
        const maxX = Math.max(minX, QQC2.Overlay.overlay.width - width - edgeMargin)
        return Math.max(minX, Math.min(desired, maxX))
    }
    y: anchorPos.y + (anchorItem ? anchorItem.height : 0) + anchorGap

    implicitWidth: 320
    implicitHeight: content.implicitHeight + 20 + arrowHeight
    width: implicitWidth
    height: implicitHeight

    onWidthChanged: backgroundCanvas.requestPaint()
    onHeightChanged: backgroundCanvas.requestPaint()
    onVisibleChanged: if (visible) backgroundCanvas.requestPaint()

    // Close on click outside
    MouseArea {
        parent: QQC2.Overlay.overlay
        anchors.fill: parent
        visible: root.visible
        z: root.z - 1
        onClicked: root.visible = false
    }

    Canvas {
        id: backgroundCanvas
        anchors.fill: parent

        function cssColor(c) {
            if (c === undefined || c === null) return "#202020"
            if (typeof c === "string") return c
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
            ctx.strokeStyle = cssColor(Qt.lighter(Kirigami.Theme.backgroundColor, 1.35))
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: arrowHeight + 14
        spacing: 8
        z: 1

        // Include Archive
        QQC2.CheckBox {
            id: includeArchive
            text: i18n("Include Archive")
            checked: false
        }

        // Has the words
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("Has the words:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: hasWordsField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // Doesn't have
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("Doesn't have:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: doesntHaveField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // From
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("From:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: fromField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // To
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("To:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: toField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // Subject
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            QQC2.Label {
                text: i18n("Subject:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.TextField {
                id: subjectField
                Layout.fillWidth: true
                font.pixelSize: 12
                implicitHeight: 28
            }
        }

        // Has Attachment
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            QQC2.Label {
                text: i18n("Has Attachment:")
                font.pixelSize: 12
                opacity: 0.8
                Layout.fillWidth: true
            }
            QQC2.Switch {
                id: hasAttachmentSwitch
            }
        }

        // Date within
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            QQC2.Label {
                text: i18n("Date within:")
                font.pixelSize: 12
                opacity: 0.8
            }
            QQC2.ComboBox {
                id: dateWithinCombo
                Layout.fillWidth: true
                model: [
                    i18n("Any time"),
                    i18n("Past week"),
                    i18n("Past month"),
                    i18n("Past 3 months"),
                    i18n("Past year")
                ]
                currentIndex: 0
                font.pixelSize: 12
            }
        }

        // Choose fields link (stub)
        QQC2.Label {
            text: "<a href='#'>" + i18n("Choose fields") + "</a>"
            font.pixelSize: 11
            opacity: 0.6
            Layout.alignment: Qt.AlignRight
            textFormat: Text.RichText
            // Stub — no action
        }

        // Bottom row: Create Search Folder... + Search button
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 8

            QQC2.Button {
                text: i18n("Create Search Folder...")
                flat: true
                enabled: false
                font.pixelSize: 12
                leftPadding: 8
                rightPadding: 8
                topPadding: 4
                bottomPadding: 4
                background: Rectangle {
                    color: parent.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                         : (parent.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22)
                                           : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                    border.width: 1
                    border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                    radius: 4
                }
            }

            Item { Layout.fillWidth: true }

            QQC2.Button {
                id: searchBtn
                text: i18n("Search")
                font.pixelSize: 12
                leftPadding: 16
                rightPadding: 16
                topPadding: 5
                bottomPadding: 5
                icon.name: "edit-find"
                contentItem: RowLayout {
                    spacing: 4
                    Kirigami.Icon {
                        source: searchBtn.icon.name
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        color: "white"
                    }
                    QQC2.Label {
                        text: searchBtn.text
                        color: "white"
                        font.pixelSize: 12
                    }
                }
                background: Rectangle {
                    color: searchBtn.down ? Qt.darker("#E67E22", 1.2)
                         : (searchBtn.hovered ? Qt.lighter("#E67E22", 1.1)
                                              : "#E67E22")
                    radius: 4
                }
                onClicked: {
                    // Build query from the "Has the words" field for now
                    const q = hasWordsField.text.trim()
                    if (q.length > 0) {
                        root.visible = false
                        root.searchTriggered(q)
                    }
                }
            }
        }
    }
}
