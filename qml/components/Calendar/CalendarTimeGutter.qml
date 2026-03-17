import QtQuick
import QtQuick.Controls as QQC2

Item {
    id: root

    property int startHour: 0
    property int endHour: 24
    property real hourHeight: 72

    readonly property int halfHourSlots: (endHour - startHour) * 2

    implicitWidth: 56
    implicitHeight: (endHour - startHour) * hourHeight

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        Repeater {
            model: root.halfHourSlots + 1

            delegate: QQC2.Label {
                required property int index
                visible: index > 0 && index < root.halfHourSlots
                x: 4
                y: index * (root.hourHeight / 2) - (height / 2)
                text: {
                    const totalHours = root.startHour + Math.floor(index / 2)
                    const isHalf = (index % 2) === 1
                    const suffix = totalHours >= 12 ? "PM" : "AM"
                    const h12 = totalHours % 12 === 0 ? 12 : totalHours % 12
                    return h12 + (isHalf ? ":30 " : ":00 ") + suffix
                }
                font.pixelSize: 12
                color: Qt.rgba(1, 1, 1, 0.75)
            }
        }
    }
}
