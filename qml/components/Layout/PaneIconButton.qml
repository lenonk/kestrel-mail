import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../Common"

QQC2.Control {
    id: root

    SystemPalette { id: systemPalette }
    property string iconName: ""
    property string label: ""
    property bool active: false
    property bool hoverFeedback: true
    property bool showLabel: true
    property string toolTipText: ""
    property bool useHorizontalDots: false
    property bool underlineOnLeft: false
    property bool underlineOnRight: false
    property int sideIndicatorInset: 0
    signal clicked

    readonly property int paneIconSize: Kirigami.Units.iconSizes.smallMedium
    readonly property int indicatorThickness: 4

    Accessible.name: root.toolTipText.length > 0 ? root.toolTipText : root.label
    Accessible.role: Accessible.Button
    implicitHeight: Kirigami.Units.gridUnit * 2 + 2
    padding: 0

    TextToolTip {
        id: hoverTip
        parent: QQC2.Overlay.overlay
        visible: mouse.containsMouse && !root.showLabel && (root.toolTipText.length > 0 || root.label.length > 0)
        toolTipText: root.toolTipText.length > 0 ? root.toolTipText : root.label

        onVisibleChanged: {
            hoverTip.preferredY = root.mapToItem(QQC2.Overlay.overlay, 0, 0).y - getYOffset();
            hoverTip.preferredX = root.mapToItem(QQC2.Overlay.overlay, getXOffset(), 0).x
        }

        function getXOffset() {
            if (root.underlineOnLeft) return root.width + Kirigami.Units.smallSpacing
            if (root.underlineOnRight) return -hoverTip.implicitWidth - Kirigami.Units.smallSpacing
            return Math.round((root.width - hoverTip.implicitWidth) / 2)
        }

        function getYOffset() {
            const isVertical = (root.underlineOnLeft || root.underlineOnRight)

            return isVertical ? -((root.height / 2) - (hoverTip.implicitHeight / 2)) :
                hoverTip.implicitHeight + Kirigami.Units.smallSpacing
        }
    }

    contentItem: Item {
        anchors.fill: parent

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.clicked()
        }

        Rectangle {
            anchors.fill: parent
            color: ((root.hoverFeedback && mouse.containsMouse) || root.active)
                   ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.5)
                   : "transparent"
        }

        Item {
            id: iconLane
            anchors.fill: parent

            Item {
                visible: !root.showLabel
                anchors.centerIn: parent
                width: root.paneIconSize
                height: root.paneIconSize

                Kirigami.Icon {
                    visible: !root.useHorizontalDots
                    anchors.centerIn: parent
                    source: root.iconName
                    width: root.paneIconSize
                    height: root.paneIconSize
                    color: Kirigami.Theme.textColor
                }

                Row {
                    visible: root.useHorizontalDots
                    anchors.centerIn: parent
                    spacing: 3
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: Kirigami.Theme.textColor
                        }
                    }
                }
            }

            Column {
                visible: root.showLabel
                anchors.centerIn: parent
                spacing: 2

                Kirigami.Icon {
                    visible: !root.useHorizontalDots
                    source: root.iconName
                    width: root.paneIconSize
                    height: root.paneIconSize
                    color: Kirigami.Theme.textColor
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Row {
                    visible: root.useHorizontalDots
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 3
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: Kirigami.Theme.textColor
                        }
                    }
                }

                QQC2.Label {
                    text: root.label
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    width: root.width
                    opacity: 0.9
                }
            }
        }

        Rectangle {
            visible: !root.underlineOnLeft && !root.underlineOnRight
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.indicatorThickness
            color: (root.active || (root.hoverFeedback && mouse.containsMouse)) ? systemPalette.highlight : "transparent"
        }

        Rectangle {
            visible: root.underlineOnLeft
            x: root.sideIndicatorInset
            y: 0
            width: root.indicatorThickness
            height: parent.height
            color: (root.active || (root.hoverFeedback && mouse.containsMouse)) ? systemPalette.highlight : "transparent"
        }

        Rectangle {
            visible: root.underlineOnRight
            x: parent.width - root.indicatorThickness - root.sideIndicatorInset
            y: 0
            width: root.indicatorThickness
            height: parent.height
            color: (root.active || (root.hoverFeedback && mouse.containsMouse)) ? systemPalette.highlight : "transparent"
        }
    }
}
