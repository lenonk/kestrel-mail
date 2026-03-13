import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    default property alias content: innerLayout.data

    Layout.fillHeight: true
    Layout.preferredWidth: 21

    ColumnLayout {
        id: innerLayout

        anchors.centerIn: parent
        spacing: 4
    }
}
