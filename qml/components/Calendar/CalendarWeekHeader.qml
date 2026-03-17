import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

Item {
    id: root

    property var dayNames: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    property var dayNumbers: [9, 10, 11, 12, 13, 14, 15]
    property int todayNumber: 15

    implicitHeight: 44

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.dayNames.length

            delegate: Rectangle {
                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: root.height
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    QQC2.Label {
                        text: String(root.dayNumbers[index])
                        font.bold: root.dayNumbers[index] === root.todayNumber
                        color: root.dayNumbers[index] === root.todayNumber ? "#fff" : Qt.rgba(1, 1, 1, 0.85)
                    }
                    QQC2.Label {
                        text: root.dayNames[index]
                        color: Qt.rgba(1, 1, 1, 0.85)
                        font.bold: true
                    }
                }
            }
        }
    }
}
