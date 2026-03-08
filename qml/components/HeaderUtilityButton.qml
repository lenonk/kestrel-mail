import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: root

    property string iconName: ""
    property int iconSize: 16
    property color highlightColor: "#3daee9"

    flat: true
    implicitWidth: 28
    implicitHeight: 28
    leftPadding: 0
    rightPadding: 0
    hoverEnabled: true

    background: Rectangle {
        radius: 4
        color: root.down ? Qt.darker(root.highlightColor, 1.22)
                        : (root.hovered ? Qt.lighter(root.highlightColor, 1.25) : "transparent")
    }

    contentItem: Item {
        Kirigami.Icon {
            anchors.centerIn: parent
            source: root.iconName
            width: root.iconSize
            height: root.iconSize
        }
    }
}
