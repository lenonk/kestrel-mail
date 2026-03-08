import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Control {
    id: root

    SystemPalette { id: systemPalette }

    property string iconName: ""
    property string text: ""
    // menuItems supports strings or objects: { text, icon }
    property var menuItems: []
    property bool alwaysHighlighted: false
    // Future icon-pack support: true=theme icons, false=app icon pack (to be implemented)
    property bool useThemeIcons: true

    readonly property bool hasMenu: root.menuItems.length > 0
    readonly property bool highlightActive: alwaysHighlighted || mouse.containsMouse
    readonly property int actionIconSize: 17
    readonly property int edgeInset: 6
    readonly property int menuGap: 2
    property bool spinning: false
    property real spinAngle: 0

    signal triggered(string actionText)

    implicitHeight: Kirigami.Units.gridUnit + 8
    implicitWidth: leftRow.implicitWidth + (hasMenu ? (arrow.width + menuGap * 2) : 0) + edgeInset * 2

    background: Rectangle {
        radius: 4
        color: root.alwaysHighlighted
               ? (mouse.containsMouse ? Qt.darker(systemPalette.highlight, 1.22) : systemPalette.highlight)
               : (mouse.containsMouse ? Qt.lighter(systemPalette.highlight, 1.25) : "transparent")
    }

    contentItem: Item {
        anchors.fill: parent

        Row {
            id: leftRow
            anchors.left: parent.left
            anchors.leftMargin: root.edgeInset
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Kirigami.Icon {
                id: actionIcon
                source: root.iconName
                width: root.actionIconSize
                height: root.actionIconSize
                color: Kirigami.Theme.textColor
                visible: !!root.iconName
                anchors.verticalCenter: parent.verticalCenter
                transform: Rotation {
                    origin.x: actionIcon.width / 2
                    origin.y: actionIcon.height / 2
                    angle: root.spinAngle
                }
            }

            QQC2.Label {
                text: root.text
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Kirigami.Icon {
            id: arrow
            source: "arrow-down"
            width: root.actionIconSize - 5
            height: root.actionIconSize - 5
            visible: root.hasMenu
            color: Kirigami.Theme.textColor
            anchors.right: parent.right
            anchors.rightMargin: root.menuGap
            anchors.verticalCenter: parent.verticalCenter
        }

        // Full-height separator for split-action look
        Rectangle {
            visible: root.hasMenu && root.highlightActive
            width: 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: arrow.x - root.menuGap
            color: systemPalette.window
            opacity: 0.95
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true

        readonly property bool inMenuGutter: root.hasMenu && mouseX >= (arrow.x - root.menuGap)

        onClicked: {
            if (root.menuItems.length > 0 && inMenuGutter) {
                popup.openIfClosed()
                return
            }

            // Main button area executes default action.
            root.triggered("")
        }
    }

    onSpinningChanged: {
        if (!spinning) spinAngle = 0
    }

    NumberAnimation on spinAngle {
        from: 0
        to: 360
        duration: 1400
        loops: Animation.Infinite
        running: root.spinning
    }

    PopupMenu {
        id: popup
        parent: root

        Repeater {
            model: root.menuItems
            delegate: QQC2.MenuItem {
                readonly property var itemData: (typeof modelData === "string") ? ({ text: modelData, icon: "" }) : modelData
                text: itemData.text || ""
                icon.name: itemData.icon || ""
                onTriggered: root.triggered(text)
            }
        }
    }
}
