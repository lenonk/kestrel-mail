import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    property alias title: titleLabel.text
    property bool titleBold: true
    property real titlePointSizeDelta: 0
    property int headerHeight: 42
    property int utilityIconSize: 16
    property color utilityHighlightColor: KestrelColors.utilityHighlight
    property string collapseIconName: "go-previous-symbolic"
    property int collapseIconSize: 24
    property int collapseButtonSize: 44
    property int collapseRightMargin: -15
    property int rowLeftMargin: 0
    property int rowRightMargin: 0

    signal collapseRequested()

    Layout.fillWidth: true
    Layout.preferredHeight: root.headerHeight
    color: Kirigami.Theme.backgroundColor

    HoverHandler { id: headerHover }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.rowLeftMargin
        anchors.rightMargin: root.rowRightMargin

        QQC2.Label {
            id: titleLabel
            font.bold: root.titleBold
            font.pointSize: Kirigami.Theme.defaultFont.pointSize + root.titlePointSizeDelta
        }

        HeaderUtilityButton {
            iconName: "edit-find"
            iconSize: root.utilityIconSize
            highlightColor: root.utilityHighlightColor
            opacity: headerHover.hovered ? 1 : 0
            enabled: headerHover.hovered
        }

        HeaderUtilityButton {
            iconName: "settings-configure"
            iconSize: root.utilityIconSize
            highlightColor: root.utilityHighlightColor
            opacity: headerHover.hovered ? 1 : 0
            enabled: headerHover.hovered
        }

        Item { Layout.fillWidth: true }

        IconOnlyFlatButton {
            implicitWidth: root.collapseButtonSize
            implicitHeight: root.collapseButtonSize
            iconName: root.collapseIconName
            iconSize: root.collapseIconSize
            Layout.rightMargin: root.collapseRightMargin
            opacity: headerHover.hovered ? 1 : 0
            enabled: headerHover.hovered
            onClicked: root.collapseRequested()
        }
    }
}
