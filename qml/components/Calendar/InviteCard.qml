import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    property string title: ""
    property string whenText: ""
    property string fromText: ""
    property bool showBottomDivider: false
    property int sectionSpacing: 8
    property int radiusValue: 4

    Layout.fillWidth: true
    Layout.preferredHeight: 132
    radius: root.radiusValue
    color: Kirigami.Theme.backgroundColor
    border.color: "transparent"

    Column {
        anchors.fill: parent
        anchors.margins: root.sectionSpacing
        spacing: root.sectionSpacing - 3

        QQC2.Label {
            text: root.title
            font.bold: true
        }

        QQC2.Label {
            text: root.whenText
            wrapMode: Text.Wrap
        }

        QQC2.Label {
            text: root.fromText
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        Row {
            spacing: 6

            Rectangle {
                width: 26
                height: 22
                radius: 4
                color: "#CFEEDB"
                border.width: 1
                border.color: "#B5DEC7"
                Kirigami.Icon { anchors.centerIn: parent; source: "dialog-ok-apply"; width: 14; height: 14 }
            }

            Rectangle {
                width: 26
                height: 22
                radius: 4
                color: "#F7D6D9"
                border.width: 1
                border.color: "#EABCC2"
                Kirigami.Icon { anchors.centerIn: parent; source: "dialog-cancel"; width: 14; height: 14 }
            }

            Rectangle {
                width: 26
                height: 22
                radius: 4
                color: "#F6EDC9"
                border.width: 1
                border.color: "#E9DFAE"
                QQC2.Label { anchors.centerIn: parent; text: "?" }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.sectionSpacing
        anchors.rightMargin: root.sectionSpacing
        anchors.bottom: parent.bottom
        height: 1
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.7)
        visible: root.showBottomDivider
    }
}
