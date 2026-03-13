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

    MouseArea {
        anchors.fill: parent
        hoverEnabled: false
        onClicked: {
            root.appRoot.categorySelectionExplicit = true
            root.appRoot.selectedCategoryIndex = root.index
        }
    }

    QQC2.Label {
        id: categoryLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        text: root.modelData
        font.bold: root.index === root.appRoot.selectedCategoryIndex
        opacity: root.index === root.appRoot.selectedCategoryIndex ? 1 : 0.75
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: root.index === root.appRoot.selectedCategoryIndex ? root.systemPalette.highlight : "transparent"
    }
}
