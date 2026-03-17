import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

Item {
    id: root

    signal itemClicked(int index, var item)

    property var items: []
    property bool vertical: false
    property bool showLabel: false
    property bool underlineOnLeft: false
    property bool underlineOnRight: false
    property int sideIndicatorInset: 0

    implicitHeight: vertical ? column.implicitHeight : row.implicitHeight
    implicitWidth: vertical ? column.implicitWidth : row.implicitWidth

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 0
        visible: root.vertical

        Repeater {
            model: root.items
            delegate: PaneIconButton {
                required property int index
                required property var modelData
                Layout.fillWidth: true
                iconName: (modelData.iconName || "")
                label: (modelData.label || "")
                toolTipText: (modelData.toolTipText || modelData.label || "")
                showLabel: root.showLabel
                active: !!modelData.active
                hoverFeedback: true
                useHorizontalDots: !!modelData.useHorizontalDots
                underlineOnLeft: root.underlineOnLeft
                underlineOnRight: root.underlineOnRight
                sideIndicatorInset: root.sideIndicatorInset
                onClicked: root.itemClicked(index, modelData)
            }
        }
    }

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 0
        visible: !root.vertical

        Repeater {
            model: root.items
            delegate: PaneIconButton {
                required property int index
                required property var modelData
                Layout.fillWidth: true
                iconName: (modelData.iconName || "")
                label: (modelData.label || "")
                toolTipText: (modelData.toolTipText || modelData.label || "")
                showLabel: root.showLabel
                active: !!modelData.active
                hoverFeedback: true
                useHorizontalDots: !!modelData.useHorizontalDots
                underlineOnLeft: root.underlineOnLeft
                underlineOnRight: root.underlineOnRight
                sideIndicatorInset: root.sideIndicatorInset
                onClicked: root.itemClicked(index, modelData)
            }
        }
    }
}
