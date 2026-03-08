import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    property color dividerColor: Qt.lighter(Kirigami.Theme.backgroundColor, 1.7)

    Layout.fillWidth: true
    Layout.minimumHeight: 1
    Layout.preferredHeight: 1
    Layout.maximumHeight: 1

    color: root.dividerColor
}
