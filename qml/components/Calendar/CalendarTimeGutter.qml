import QtQuick
import QtQuick.Controls as QQC2

Item {
    id: root

    property int startHour: 8
    property int endHour: 18
    property real hourHeight: 72
    property real topInset: 0

    implicitWidth: 56

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        Repeater {
            model: root.endHour - root.startHour + 1

            delegate: QQC2.Label {
                required property int index
                x: 4
                y: root.topInset + index * root.hourHeight - (height / 2)
                text: {
                    const h24 = root.startHour + index
                    const suffix = h24 >= 12 ? "PM" : "AM"
                    const h12 = h24 % 12 === 0 ? 12 : h24 % 12
                    return h12 + ":00 " + suffix
                }
                font.pixelSize: 12
                color: Qt.rgba(1, 1, 1, 0.75)
            }
        }
    }
}
