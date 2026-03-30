import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root
    implicitWidth: categoryLabel.implicitWidth + 50
    implicitHeight: Math.round((categoryLabel.implicitHeight + 3) * 1.5)

    required property var appRoot
    required property var systemPalette
    required property int index
    required property var modelData
    property int newMessageCount: 0

    MouseArea {
        anchors.fill: parent
        hoverEnabled: false
        onClicked: {
            root.appRoot.categorySelectionExplicit = true
            root.appRoot.selectedCategoryIndex = root.index
        }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        QQC2.Label {
            id: categoryLabel
            text: root.modelData
            font.bold: root.index === root.appRoot.selectedCategoryIndex
            opacity: root.index === root.appRoot.selectedCategoryIndex ? 1 : 0.75
        }

        QQC2.Label {
            visible: root.newMessageCount > 0
            text: "+" + root.newMessageCount
            color: Kirigami.Theme.positiveTextColor
            font.bold: true
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: root.index === root.appRoot.selectedCategoryIndex ? root.systemPalette.highlight : "transparent"
    }
}
