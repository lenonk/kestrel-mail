import QtQuick
import QtQuick.Layouts
import QtWebEngine
import QtQuick.Pdf
import org.kde.kirigami as Kirigami
import ".."

ArrowPopup {
    id: root

    property bool targetHovered: false
    property bool downloadComplete: false
    property int downloadProgress: 0
    property string fallbackIcon: "mail-attachment"
    property string previewMimeType: ""
    property string previewSource: ""
    property int maxPreviewHeight: 112
    property int maxPreviewWidth: 112
    property string openButtonText: i18n("Open")
    property string saveButtonText: i18n("Save")

    readonly property string previewLower: (previewSource || "").toLowerCase()
    readonly property string previewUrl: previewSource.length > 0 ? ("file://" + encodeURI(previewSource)) : ""
    readonly property bool isImagePreview: previewMimeType.toLowerCase().indexOf("image/") === 0 || previewLower.endsWith(".png") || previewLower.endsWith(".jpg") || previewLower.endsWith(".jpeg") || previewLower.endsWith(".webp") || previewLower.endsWith(".gif") || previewLower.endsWith(".bmp")
    readonly property bool isPdfPreview: previewMimeType.toLowerCase().indexOf("pdf") >= 0 || previewLower.endsWith(".pdf")
    readonly property bool isWebPreview: !isImagePreview && !isPdfPreview && (previewLower.endsWith(".txt") || previewLower.endsWith(".md") || previewLower.endsWith(".html") || previewLower.endsWith(".htm"))
    readonly property bool shown: targetHovered || hoverProbe.hovered

    signal openTriggered
    signal saveTriggered

    function borderForTheme() {
        const c = Kirigami.Theme.backgroundColor;
        const luminance = (0.2126 * c.r) + (0.7152 * c.g) + (0.0722 * c.b);
        // In dark themes, force a clearly visible light border.
        return luminance < 0.5 ? Qt.rgba(1, 1, 1, 0.42) : Qt.darker(c, 2.0);
    }

    borderColor: Qt.darker(Kirigami.Theme.disabledTextColor, 2)
    visible: shown
    implicitWidth: 258
    width: implicitWidth

    HoverHandler {
        id: hoverProbe
        margin: Math.max(0, root.anchorGap)
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

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

                StyledButton {
                    id: openBtn

                    bottomPadding: 4
                    icon.name: "document-open"
                    leftPadding: 8
                    rightPadding: 10
                    text: root.openButtonText
                    topPadding: 4
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize + 2

                    onClicked: root.openTriggered()
                }
                StyledButton {
                    id: saveBtn

                    bottomPadding: 4
                    icon.name: "document-save"
                    leftPadding: 8
                    rightPadding: 10
                    text: root.saveButtonText
                    topPadding: 4
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize + 2

                    onClicked: root.saveTriggered()
                }
            }
        }
    }
}
