import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import QtQuick.Pdf
import org.kde.kirigami as Kirigami

Item {
    id: root

    readonly property real anchorCenterX: anchorPos.x + (anchorItem ? anchorItem.width / 2 : 0)
    property real anchorGap: Kirigami.Units.smallSpacing
    property Item anchorItem: null
    property point anchorPos: visible && anchorItem ? anchorItem.mapToItem(QQC2.Overlay.overlay, 0, 0) : Qt.point(0, 0)
    property real arrowHeight: 8
    property real arrowLeftPx: 70
    property real arrowWidth: 18
    property bool downloadComplete: false
    property int downloadProgress: 0
    property real edgeMargin: 6
    property string fallbackIcon: "mail-attachment"
    readonly property bool isImagePreview: previewMimeType.toLowerCase().indexOf("image/") === 0 || previewLower.endsWith(".png") || previewLower.endsWith(".jpg") || previewLower.endsWith(".jpeg") || previewLower.endsWith(".webp") || previewLower.endsWith(".gif") || previewLower.endsWith(".bmp")
    readonly property bool isPdfPreview: previewMimeType.toLowerCase().indexOf("pdf") >= 0 || previewLower.endsWith(".pdf")
    readonly property bool isWebPreview: !isImagePreview && !isPdfPreview && (previewLower.endsWith(".txt") || previewLower.endsWith(".md") || previewLower.endsWith(".html") || previewLower.endsWith(".htm"))
    property int maxPreviewHeight: 112
    property int maxPreviewWidth: 112
    property string openButtonText: i18n("Open")
    readonly property string previewLower: (previewSource || "").toLowerCase()
    property string previewMimeType: ""
    property string previewSource: ""
    readonly property string previewUrl: previewSource.length > 0 ? ("file://" + encodeURI(previewSource)) : ""
    property string saveButtonText: i18n("Save")
    readonly property bool shown: targetHovered || hoverProbe.hovered
    property bool targetHovered: false

    signal openTriggered
    signal saveTriggered

    function borderForTheme() {
        const c = Kirigami.Theme.backgroundColor;
        const luminance = (0.2126 * c.r) + (0.7152 * c.g) + (0.0722 * c.b);
        // In dark themes, force a clearly visible light border.
        return luminance < 0.5 ? Qt.rgba(1, 1, 1, 0.42) : Qt.darker(c, 2.0);
    }

    height: implicitHeight
    implicitHeight: content.implicitHeight + 20 + arrowHeight
    implicitWidth: 258
    parent: QQC2.Overlay.overlay
    visible: shown
    width: implicitWidth
    x: {
        const arrowCenterFromLeft = arrowLeftPx + (arrowWidth / 2);
        const desired = anchorCenterX - arrowCenterFromLeft;
        const minX = edgeMargin;
        const maxX = Math.max(minX, QQC2.Overlay.overlay.width - width - edgeMargin);
        return Math.max(minX, Math.min(desired, maxX));
    }
    y: anchorPos.y + (anchorItem ? anchorItem.height : 0) + anchorGap
    z: 2000

    onVisibleChanged: if (visible)
        backgroundCanvas.requestPaint()

    Canvas {
        id: backgroundCanvas

        function cssColor(c) {
            if (c === undefined || c === null)
                return "#202020";
            if (typeof c === "string")
                return c;
            if (c.r !== undefined && c.g !== undefined && c.b !== undefined) {
                const a = (c.a !== undefined) ? c.a : 1;
                return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + a + ")";
            }
            return "" + c;
        }

        anchors.fill: parent

        Component.onCompleted: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.clearRect(0, 0, width, height);

            const ax = Math.max(8, Math.min(root.arrowLeftPx, width - root.arrowWidth - 8));

            ctx.beginPath();
            ctx.moveTo(ax + root.arrowWidth / 2, 0);
            ctx.lineTo(ax, root.arrowHeight);
            ctx.lineTo(0, root.arrowHeight);
            ctx.lineTo(0, height);
            ctx.lineTo(width, height);
            ctx.lineTo(width, root.arrowHeight);
            ctx.lineTo(ax + root.arrowWidth, root.arrowHeight);
            ctx.closePath();
            ctx.fillStyle = cssColor(Kirigami.Theme.backgroundColor);
            ctx.fill();

            ctx.beginPath();
            ctx.moveTo(ax + root.arrowWidth / 2, 0);
            ctx.lineTo(ax, root.arrowHeight);
            ctx.lineTo(0, root.arrowHeight);
            ctx.lineTo(0, height);
            ctx.lineTo(width, height);
            ctx.lineTo(width, root.arrowHeight);
            ctx.lineTo(ax + root.arrowWidth, root.arrowHeight);
            ctx.closePath();
            ctx.lineWidth = 1;
            ctx.strokeStyle = cssColor(Qt.darker(Kirigami.Theme.disabledTextColor, 2));
            ctx.stroke();
        }
    }
    HoverHandler {
        id: hoverProbe

        margin: Math.max(0, root.anchorGap)
    }
    ColumnLayout {
        id: content

        anchors.bottomMargin: 10
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.top: parent.top
        anchors.topMargin: arrowHeight + 10
        spacing: 8
        z: 1

        Rectangle {
            Layout.fillWidth: true
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.25)
            height: 5
            radius: 2
            visible: !root.downloadComplete

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.top: parent.top
                color: Kirigami.Theme.highlightColor
                radius: 2
                width: parent.width * (Math.max(0, Math.min(100, root.downloadProgress)) / 100.0)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            opacity: root.downloadComplete ? 1.0 : 0.0
            spacing: 10

            Behavior on opacity {
                NumberAnimation {
                    duration: 180
                }
            }

            Rectangle {
                Layout.preferredHeight: {
                    if (previewImg.visible)
                        return Math.min(root.maxPreviewHeight, Math.max(24, previewImg.paintedHeight + 2));
                    if (pdfPreview.visible)
                        return Math.min(root.maxPreviewHeight, Math.max(24, pdfPreview.paintedHeight + 2));
                    return root.maxPreviewHeight;
                }
                Layout.preferredWidth: {
                    if (previewImg.visible)
                        return Math.min(root.maxPreviewWidth, Math.max(24, previewImg.paintedWidth + 2));
                    if (pdfPreview.visible)
                        return Math.min(root.maxPreviewWidth, Math.max(24, pdfPreview.paintedWidth + 2));
                    return root.maxPreviewWidth;
                }
                border.color: root.borderForTheme()
                border.width: 1
                clip: true
                color: Qt.darker(Kirigami.Theme.backgroundColor, 1.08)

                Image {
                    id: previewImg

                    anchors.fill: parent
                    anchors.margins: parent.border.width
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    source: root.isImagePreview ? root.previewUrl : ""
                    sourceSize.height: root.maxPreviewHeight
                    sourceSize.width: root.maxPreviewWidth
                    verticalAlignment: Image.AlignVCenter
                    visible: root.isImagePreview && status === Image.Ready
                }

                PdfDocument {
                    id: pdfDoc

                    source: root.isPdfPreview ? root.previewUrl : ""
                }

                PdfPageImage {
                    id: pdfPreview

                    anchors.fill: parent
                    anchors.margins: parent.border.width
                    asynchronous: true
                    currentFrame: 0
                    document: pdfDoc
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignVCenter
                    visible: root.isPdfPreview && status === Image.Ready
                }

                WebEngineView {
                    id: previewWeb

                    anchors.fill: parent
                    anchors.margins: parent.border.width
                    backgroundColor: "transparent"
                    settings.javascriptEnabled: false
                    settings.localContentCanAccessFileUrls: true
                    url: root.isWebPreview ? root.previewUrl : ""
                    visible: root.isWebPreview
                }

                Kirigami.Icon {
                    anchors.centerIn: parent
                    height: 32
                    opacity: 0.7
                    source: root.fallbackIcon
                    visible: !previewImg.visible && !pdfPreview.visible && !previewWeb.visible
                    width: 32
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop | Qt.AlignRight
                spacing: 8

                QQC2.Button {
                    id: openBtn

                    bottomPadding: 4
                    flat: true
                    icon.name: "document-open"
                    leftPadding: 8
                    rightPadding: 10
                    text: root.openButtonText
                    topPadding: 4

                    background: Rectangle {
                        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                        border.width: 1
                        color: openBtn.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35) : (openBtn.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22) : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                        radius: 0
                    }
                    contentItem: RowLayout {
                        spacing: 8

                        Kirigami.Icon {
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 16
                            source: openBtn.icon.name
                        }
                        QQC2.Label {
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize + 2
                            text: openBtn.text
                        }
                    }

                    onClicked: root.openTriggered()
                }
                QQC2.Button {
                    id: saveBtn

                    bottomPadding: 4
                    flat: true
                    icon.name: "document-save"
                    leftPadding: 8
                    rightPadding: 10
                    text: root.saveButtonText
                    topPadding: 4

                    background: Rectangle {
                        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                        border.width: 1
                        color: saveBtn.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35) : (saveBtn.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22) : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
                        radius: 0
                    }
                    contentItem: RowLayout {
                        spacing: 8

                        Kirigami.Icon {
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 16
                            source: saveBtn.icon.name
                        }
                        QQC2.Label {
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize + 2
                            text: saveBtn.text
                        }
                    }

                    onClicked: root.saveTriggered()
                }
            }
        }
    }
}
