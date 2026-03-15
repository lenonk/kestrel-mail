import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: root
    required property bool isHeaderRow
    required property string modelTitle
    required property bool modelExpanded
    required property var modelBucketKey
    required property var appRoot

    visible: isHeaderRow
    width: parent.width
    implicitHeight: Kirigami.Units.gridUnit + 2
    y: 10
    flat: true
    leftPadding: 0
    rightPadding: 0
    hoverEnabled: false
    background: Item {}

    contentItem: RowLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing
        Kirigami.Icon {
            source: root.modelExpanded ? "go-down-symbolic" : "go-next-symbolic"
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
        }
        QQC2.Label {
            text: root.modelTitle || ""
            font.bold: true
            color: Kirigami.Theme.textColor
        }
        Item { Layout.fillWidth: true }
    }

    onClicked: root.appRoot.setBucketExpanded(root.modelBucketKey, !root.modelExpanded)
}
