import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: root

    property int radius: 0
    property int iconSize: 16
    property int iconLabelSpacing: 8

    flat: true

    background: Rectangle {
        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
        border.width: 1
        color: root.down ? Qt.darker(Kirigami.Theme.backgroundColor, 1.35)
                         : (root.hovered ? Qt.darker(Kirigami.Theme.backgroundColor, 1.22)
                                         : Qt.darker(Kirigami.Theme.backgroundColor, 1.12))
        radius: root.radius
    }

    contentItem: RowLayout {
        spacing: root.iconLabelSpacing

        Kirigami.Icon {
            Layout.preferredHeight: root.iconSize
            Layout.preferredWidth: root.iconSize
            source: root.icon.name
            visible: root.icon.name.length > 0
        }
        QQC2.Label {
            color: Kirigami.Theme.textColor
            font: root.font
            text: root.text
            visible: root.text.length > 0
        }
    }
}
