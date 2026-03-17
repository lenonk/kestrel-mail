import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string rangeLabel: "March 9 - 15, 2026"
    property int activeIndex: 2
    signal prevRequested
    signal nextRequested

    implicitHeight: 44

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        QQC2.ToolButton { icon.name: "go-previous"; onClicked: root.prevRequested() }
        QQC2.ToolButton { icon.name: "go-next"; onClicked: root.nextRequested() }

        QQC2.Label {
            text: root.rangeLabel
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }

        Item { Layout.fillWidth: true }

        Repeater {
            model: [i18n("Day"), i18n("Work Week"), i18n("Week"), i18n("Month"), i18n("Agenda")]

            delegate: QQC2.ToolButton {
                required property int index
                required property string modelData
                text: modelData
                checkable: true
                checked: index === root.activeIndex
            }
        }
    }
}
