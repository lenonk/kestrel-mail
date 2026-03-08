import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: root

    property string iconName: ""
    property int iconSize: 24

    flat: true
    leftPadding: 0
    rightPadding: 0
    hoverEnabled: false
    background: Item {}

    contentItem: Item {
        Kirigami.Icon {
            anchors.centerIn: parent
            source: root.iconName
            width: root.iconSize
            height: root.iconSize
        }
    }
}
