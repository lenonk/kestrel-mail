import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            text: i18n("Events")
            font.bold: true
            color: Kirigami.Theme.highlightColor
        }

        // TODO: Wire "Disable HTML format in Description" to backend
        QQC2.CheckBox {
            text: i18n("Disable HTML format in Description")
            checked: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
        }

        Item { Layout.fillHeight: true }
    }
}
