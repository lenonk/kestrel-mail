import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Reusable attachment chip for both compose (showRemoveButton: true) and
// received messages (showRemoveButton: false).
Rectangle {
    id: root

    // ── Public API ──────────────────────────────────────────────────────────
    property string attachmentName:     ""
    property string attachmentMimeType: ""
    property int    attachmentBytes:    0      // 0 = hide size label
    property bool   selected:           false  // highlight state for received view
    property bool   showRemoveButton:   false  // true in compose mode
    property bool   hovered:            chipMouse.containsMouse

    signal removeClicked
    signal chipClicked(var mouse)
    signal chipDoubleClicked(var mouse)
    signal chipEntered

    // ── Derived ─────────────────────────────────────────────────────────────
    readonly property string _sizeText: {
        const n = Number(attachmentBytes) || 0
        if (n <= 0) return ""
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1).replace(/\.0$/, "") + " kB"
        return (n / (1024 * 1024)).toFixed(1).replace(/\.0$/, "") + " MB"
    }

    readonly property string _icon: {
        const mt = (attachmentMimeType || "").toLowerCase()
        const nm = (attachmentName || "").toLowerCase()
        if (mt.startsWith("image/") || /\.(png|jpg|jpeg|gif|bmp|webp|svg|tiff|ico)$/.test(nm)) return "image-x-generic"
        if (mt === "application/pdf" || nm.endsWith(".pdf")) return "application-pdf"
        if (mt.includes("word")  || nm.endsWith(".doc")  || nm.endsWith(".docx")) return "x-office-document"
        if (mt.includes("spreadsheet") || nm.endsWith(".xls") || nm.endsWith(".xlsx")) return "x-office-spreadsheet"
        if (mt.includes("presentation") || nm.endsWith(".ppt") || nm.endsWith(".pptx")) return "x-office-presentation"
        if (mt === "application/zip" || /\.(zip|rar|7z|gz|tar|bz2|xz)$/.test(nm)) return "application-zip"
        if (mt.startsWith("audio/") || /\.(mp3|flac|ogg|wav|m4a|aac|opus)$/.test(nm)) return "audio-x-generic"
        if (mt.startsWith("video/") || /\.(mp4|mkv|avi|mov|webm|flv|wmv)$/.test(nm)) return "video-x-generic"
        if (mt.startsWith("text/")  || /\.(txt|md|csv|log|json|xml|html|htm|css|js|ts|py|c|cpp|h|rs|go)$/.test(nm)) return "text-x-generic"
        return "mail-attachment"
    }

    // ── Appearance ──────────────────────────────────────────────────────────
    SystemPalette { id: pal }

    height: 28
    radius: height / 2
    width: _chipRow.implicitWidth + 18

    border.color: root.selected
                  ? pal.highlight
                  : (root.hovered ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.55)
                                  : Qt.lighter(Kirigami.Theme.backgroundColor, 1.35))
    border.width: 1
    color: root.selected
           ? Qt.lighter(pal.highlight, 1.1)
           : (root.hovered ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
                           : Qt.lighter(Kirigami.Theme.backgroundColor, 1.12))

    RowLayout {
        id: _chipRow
        anchors.left:            parent.left
        anchors.leftMargin:      9
        anchors.verticalCenter:  parent.verticalCenter
        spacing: 6

        Kirigami.Icon {
            Layout.preferredHeight: 16
            Layout.preferredWidth:  16
            source: root._icon
        }
        QQC2.Label {
            elide:            Text.ElideRight
            font.pixelSize:   Kirigami.Theme.smallFont.pixelSize + 2
            maximumLineCount: 1
            text:             root.attachmentName
        }
        QQC2.Label {
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            opacity:        0.65
            text:           "(" + root._sizeText + ")"
            visible:        root._sizeText.length > 0 && !root.showRemoveButton
        }
        // Remove button shown in compose mode
        Text {
            visible:      root.showRemoveButton
            text:         "×"
            font.pixelSize: 13
            color:        Kirigami.Theme.disabledTextColor
            MouseArea {
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                onClicked:    root.removeClicked()
            }
        }
    }

    MouseArea {
        id: chipMouse
        anchors.fill:  parent
        cursorShape:   Qt.PointingHandCursor
        hoverEnabled:  true
        acceptedButtons: root.showRemoveButton ? Qt.NoButton : (Qt.LeftButton | Qt.RightButton)

        onEntered:      root.chipEntered()
        onClicked:      (mouse) => root.chipClicked(mouse)
        onDoubleClicked:(mouse) => root.chipDoubleClicked(mouse)
    }
}
