import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    Layout.preferredWidth: 18
    Layout.fillHeight: true
    spacing: 4

    default property alias content: innerLayout.data

    Item { Layout.fillHeight: true }

    ColumnLayout {
        id: innerLayout
        Layout.alignment: Qt.AlignHCenter
        spacing: 4
    }

    Item { Layout.fillHeight: true }
}
