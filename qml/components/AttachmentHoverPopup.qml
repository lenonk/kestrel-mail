import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import QtQuick.Pdf
import org.kde.kirigami as Kirigami

Item {
    id: root

    property Item anchorItem: null
    property bool targetHovered: false
    property string previewSource: ""
    property string previewMimeType: ""
    property string fallbackIcon: "mail-attachment"
    property string openButtonText: i18n("Open")
    property string saveButtonText: i18n("Save")
    property int downloadProgress: 0
    property bool downloadComplete: false

    property real edgeMargin: 6
    property real anchorGap: Kirigami.Units.smallSpacing
    property real arrowHeight: 8
    property real arrowWidth: 18
    property real arrowLeftPx: 70
    property int previewWidth: 112
    property int previewHeight: 112

    readonly property string previewLower: (previewSource || "").toLowerCase()
    readonly property string previewUrl: previewSource.length > 0 ? ("file://" + encodeURI(previewSource)) : ""
    readonly property bool isImagePreview: previewMimeType.toLowerCase().indexOf("image/") === 0
                                           || previewLower.endsWith(".png")
                                           || previewLower.endsWith(".jpg")
                                           || previewLower.endsWith(".jpeg")
                                           || previewLower.endsWith(".webp")
                                           || previewLower.endsWith(".gif")
                                           || previewLower.endsWith(".bmp")
    readonly property bool isPdfPreview: previewMimeType.toLowerCase().indexOf("pdf") >= 0 || previewLower.endsWith(".pdf")
    readonly property bool isWebPreview: !isImagePreview && !isPdfPreview && (previewLower.endsWith(".txt")
                                                                               || previewLower.endsWith(".md")
                                                                               || previewLower.endsWith(".html")
                                                                               || previewLower.endsWith(".htm"))

    function borderForTheme() {
        const c = Kirigami.Theme.backgroundColor;
        const luminance = (0.2126 * c.r) + (0.7152 * c.g) + (0.0722 * c.b);
        // In dark themes, force a clearly visible light border.
        return luminance < 0.5 ? Qt.rgba(1, 1, 1, 0.42) : Qt.darker(c, 2.0);
    }

    signal openTriggered()
    signal saveTriggered()

    parent: QQC2.Overlay.overlay
    z: 2000

    property point anchorPos: visible && anchorItem ? anchorItem.mapToItem(QQC2.Overlay.overlay, 0, 0) : Qt.point(0, 0)
    readonly property real anchorCenterX: anchorPos.x + (anchorItem ? anchorItem.width / 2 : 0)
    readonly property bool shown: targetHovered || hoverProbe.hovered

    visible: shown
    onVisibleChanged: if (visible) backgroundCanvas.requestPaint()

    x: {
        const arrowCenterFromLeft = arrowLeftPx + (arrowWidth / 2)
        const desired = anchorCenterX - arrowCenterFromLeft
        const minX = edgeMargin
        const maxX = Math.max(minX, QQC2.Overlay.overlay.width - width - edgeMargin)
        return Math.max(minX, Math.min(desired, maxX))
    }
    y: anchorPos.y + (anchorItem ? anchorItem.height : 0) + anchorGap

    implicitWidth: 258
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
            ctx.strokeStyle = cssColor(root.borderForTheme())
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
    }

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
        anchors.bottomMargin: 10
        spacing: 8
        z: 1

        Rectangle {
            Layout.fillWidth: true
            height: 5
            radius: 2
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.25)
            visible: !root.downloadComplete

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * (Math.max(0, Math.min(100, root.downloadProgress)) / 100.0)
                radius: 2
                color: Kirigami.Theme.highlightColor
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            opacity: root.downloadComplete ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }

            Rectangle {
                Layout.preferredWidth: root.previewWidth
                Layout.preferredHeight: root.previewHeight
                color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)
                border.width: 1
                border.color: root.borderForTheme()
                clip: true

                Image {
                    id: previewImg
                    anchors.fill: parent
                    anchors.margins: 0
                    source: root.isImagePreview ? root.previewUrl : ""
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignTop
                    asynchronous: true
                    cache: false
                    visible: root.isImagePreview && status === Image.Ready
                }

                PdfDocument {
                    id: pdfDoc
                    source: root.isPdfPreview ? root.previewUrl : ""
                }

                PdfPageImage {
                    id: pdfPreview
                    anchors.fill: parent
                    anchors.margins: 0
                    document: pdfDoc
                    currentFrame: 0
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignTop
                    asynchronous: true
                    visible: root.isPdfPreview && status === Image.Ready
                }

                WebEngineView {
                    id: previewWeb
                    anchors.fill: parent
                    anchors.margins: 0
                    visible: root.isWebPreview
                    url: root.isWebPreview ? root.previewUrl : ""
                    settings.javascriptEnabled: false
                    settings.localContentCanAccessFileUrls: true
                    backgroundColor: "transparent"
                }

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    source: root.fallbackIcon
                    opacity: 0.7
                    visible: !previewImg.visible && !pdfPreview.visible && !previewWeb.visible
                }
            }

            ColumnLayout {
                spacing: 8
                Layout.alignment: Qt.AlignTop

                QQC2.Button {
                    id: openBtn
                    text: root.openButtonText
                    icon.name: "document-open"
                    flat: true
                    leftPadding: 8
                    rightPadding: 10
                    topPadding: 4
                    bottomPadding: 4
                    onClicked: root.openTriggered()
                    contentItem: RowLayout {
                        spacing: 8
                        Kirigami.Icon {
                            source: openBtn.icon.name
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }
                        QQC2.Label {
                            text: openBtn.text
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize + 2
                        }
                    }
                    background: Rectangle {
                        color: openBtn.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                                          : (openBtn.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22)
                                                             : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                        border.width: 1
                        border.color: root.borderForTheme()
                        radius: 0
                    }
                }

                QQC2.Button {
                    id: saveBtn
                    text: root.saveButtonText
                    icon.name: "document-save"
                    flat: true
                    leftPadding: 8
                    rightPadding: 10
                    topPadding: 4
                    bottomPadding: 4
                    onClicked: root.saveTriggered()
                    contentItem: RowLayout {
                        spacing: 8
                        Kirigami.Icon {
                            source: saveBtn.icon.name
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }
                        QQC2.Label {
                            text: saveBtn.text
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize + 2
                        }
                    }
                    background: Rectangle {
                        color: saveBtn.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                                          : (saveBtn.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22)
                                                             : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                        border.width: 1
                        border.color: root.borderForTheme()
                        radius: 0
                    }
                }
            }
        }
    }
}
