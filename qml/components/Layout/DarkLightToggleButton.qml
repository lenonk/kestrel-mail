import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.AbstractButton {
    id: root

    required property bool darkMode

    signal modeToggled(bool darkMode)

    Accessible.name: root.darkMode ? "Switch to light mode" : "Switch to dark mode"
    Accessible.role: Accessible.Button
    implicitWidth: 62
    implicitHeight: 30
    hoverEnabled: true

    onClicked: {
        const next = !root.darkMode
        root.modeToggled(next)
    }

    background: Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.22)
        color: root.darkMode
               ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.10)
               : Qt.darker(Kirigami.Theme.backgroundColor, 1.05)

        // Sun icon (left)
        QQC2.Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 5
            text: "☀"
            z: 100
            font.pixelSize: 22
            color: Kirigami.Theme.textColor
        }

        // Moon icon (right)
        QQC2.Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 3
            text: "☾"
            z: 100
            font.pixelSize: 22
            color: Kirigami.Theme.textColor
        }

        // Sliding thumb
        Rectangle {
            id: thumb
            width: 24
            height: 24
            radius: 12
            y: (parent.height - height) / 2
            x: root.darkMode ? (parent.width - width - 3) : 3
            z: 50

            color: Kirigami.Theme.highlightColor
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.22)

            Behavior on x {
                NumberAnimation {
                    duration: 150
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
}
