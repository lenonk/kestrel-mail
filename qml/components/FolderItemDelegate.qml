import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.ItemDelegate {
    id: root

    property string folderKey: ""
    property string folderName: ""
    property string folderIcon: "folder"
    property int unreadCount: 0
    property color accentColor: "transparent"
    property color iconColor: "transparent"
    property bool selected: false
    property int indentLevel: 0
    property bool hasChildren: false
    property bool expanded: true
    property string tooltipText: ""
    property int rowHeight: 40
    property int iconSize: 17
    readonly property int childIndentStep: 20

    signal activated()
    signal toggleRequested()

    Layout.fillWidth: true
    implicitHeight: rowHeight
    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    clip: false
    hoverEnabled: true

    background: Rectangle {
        x: 0
        y: 0
        width: parent ? parent.width + 3 : 0
        height: parent ? parent.height : 0
        color: root.selected ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.5) : "transparent"
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Kirigami.Units.gridUnit + 2 + (root.indentLevel * root.childIndentStep)
        anchors.rightMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            visible: root.hasChildren
            source: root.expanded ? "go-down-symbolic" : "go-next-symbolic"
            Layout.preferredWidth: visible ? Math.max(12, root.iconSize - 4) : 0
            Layout.preferredHeight: visible ? Math.max(12, root.iconSize - 4) : 0
            Layout.alignment: Qt.AlignVCenter
        }

        Kirigami.Icon {
            source: root.folderIcon
            color: root.iconColor
            Layout.preferredWidth: root.iconSize
            Layout.preferredHeight: root.iconSize
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Label {
            id: folderNameLabel
            text: root.folderName
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Label {
            text: root.unreadCount > 0 ? root.unreadCount : ""
            opacity: 0.75
            horizontalAlignment: Text.AlignRight
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
    }

    onClicked: {
        if (root.hasChildren) root.toggleRequested()
        else root.activated()
    }

    function positionTooltip() {
        const xOffset = Kirigami.Units.gridUnit + 2 + root.iconSize + Kirigami.Units.smallSpacing
        const yOffset = root.height + Kirigami.Units.smallSpacing
        const mapped = root.mapToItem(QQC2.Overlay.overlay, xOffset, yOffset)
        folderTip.preferredX = mapped.x
        folderTip.preferredY = mapped.y
    }

    onHoveredChanged: {
        hovered ? folderTip.show() : folderTip.hide()
    }

    onHeightChanged: {
        if (hovered) {
            positionTooltip()
        }
    }

    onWidthChanged: {
        if (hovered) {
            positionTooltip()
        }
    }

    TextToolTip {
        id: folderTip
        parent: QQC2.Overlay.overlay
        delay: 750
        toolTipText: root.tooltipText
        onVisibleChanged: {
            if (visible) {
                Qt.callLater(root.positionTooltip)
            }
        }
    }
}
