import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    property bool railVisible: true
    property string expandIconName: "go-next-symbolic"
    property var railIcons: []
    property var navItems: []
    property bool underlineOnLeft: false
    property bool underlineOnRight: false
    property int sideIndicatorInset: 4

    signal expandRequested()

    anchors.fill: parent
    anchors.topMargin: Kirigami.Units.smallSpacing + 1
    anchors.bottomMargin: Kirigami.Units.smallSpacing + 2
    anchors.leftMargin: 0
    anchors.rightMargin: 0
    visible: root.railVisible
    spacing: 0

    IconOnlyFlatButton {
        implicitWidth: parent.width
        implicitHeight: 44
        iconName: root.expandIconName
        iconSize: 24
        onClicked: root.expandRequested()
    }

    Repeater {
        model: root.railIcons
        delegate: IconOnlyFlatButton {
            implicitWidth: parent.width
            implicitHeight: 44
            iconName: modelData
            iconSize: 24
            topPadding: 0
            bottomPadding: 0
            leftInset: 0
            rightInset: 0
            topInset: 0
            bottomInset: 0
            clip: false
        }
    }

    Item { Layout.fillHeight: true }

    PaneDivider {}

    PaneIconStrip {
        Layout.fillWidth: true
        vertical: true
        showLabel: false
        underlineOnLeft: root.underlineOnLeft
        underlineOnRight: root.underlineOnRight
        sideIndicatorInset: root.sideIndicatorInset
        items: root.navItems
    }
}
