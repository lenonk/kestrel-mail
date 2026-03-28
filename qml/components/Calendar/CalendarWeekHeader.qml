import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

Item {
    id: root

    property var dayNames: ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
    property var dayNumbers: [9, 10, 11, 12, 13, 14, 15]
    property int todayIndex: -1   // 0-6 for Mon-Sun, -1 if today is not in this week

    implicitHeight: 36

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.dayNames.length

            delegate: Rectangle {
                required property int index
                readonly property bool isToday: index === root.todayIndex

                Layout.fillWidth: true
                Layout.preferredHeight: root.height
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    // Day number — circled blue if today
                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: isToday ? "#2979ff" : "transparent"
                        anchors.verticalCenter: parent.verticalCenter

                        QQC2.Label {
                            anchors.centerIn: parent
                            text: String(root.dayNumbers[index] || "")
                            font.pixelSize: 14
                            font.bold: true
                            color: isToday ? "#ffffff" : Qt.rgba(1, 1, 1, 0.85)
                        }
                    }

                    QQC2.Label {
                        text: root.dayNames[index] || ""
                        font.pixelSize: 13
                        color: isToday ? "#2979ff" : Qt.rgba(1, 1, 1, 0.85)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
