import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root
    implicitWidth: categoryLabel.implicitWidth + 50
    implicitHeight: Math.round((categoryLabel.implicitHeight + 3) * 1.5)

    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight

    required property var appRoot
    required property var systemPalette
    required property string categoryName
    required property int categoryIndex

    Rectangle {
        anchors.fill: parent
        color: root.categoryIndex === root.appRoot.selectedCategoryIndex ? root.systemPalette.highlight : "transparent"
        opacity: 0.1
        visible: root.categoryIndex === root.appRoot.selectedCategoryIndex
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            root.appRoot.categorySelectionExplicit = true
            root.appRoot.selectedCategoryIndex = root.categoryIndex
        }
    }

    QQC2.Label {
        id: categoryLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        text: root.categoryName
        font.bold: root.categoryIndex === root.appRoot.selectedCategoryIndex
        opacity: root.categoryIndex === root.appRoot.selectedCategoryIndex ? 1 : 0.75
        color: Kirigami.Theme.textColor
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: root.categoryIndex === root.appRoot.selectedCategoryIndex ? root.systemPalette.highlight : "transparent"
    }
}
