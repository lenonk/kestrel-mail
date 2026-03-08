import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: root

    property string iconName: ""
    property int buttonWidth: Kirigami.Units.gridUnit + 10
    property int buttonHeight: Kirigami.Units.gridUnit + 10
    property int iconSize: Kirigami.Units.gridUnit + 3
    property color highlightColor: "#5b8cff"
    property color iconColor: Kirigami.Theme.textColor
    property int cornerRadius: 0

    implicitWidth: buttonWidth
    implicitHeight: buttonHeight
    leftPadding: 0
    rightPadding: 0

    background: Rectangle {
        radius: root.cornerRadius
        color: root.down ? Qt.darker(root.highlightColor, 1.45)
                        : (root.hovered ? Qt.darker(root.highlightColor, 1.7) : "transparent")
    }

    contentItem: Item {
        Kirigami.Icon {
            anchors.centerIn: parent
            source: root.iconName
            width: root.iconSize
            height: root.iconSize
            color: root.iconColor
        }
    }
}
