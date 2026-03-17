import QtQuick
import QtQuick.Controls as QQC2

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""

    radius: 4
    color: "#9a8cff"
    border.width: 1
    border.color: Qt.darker(color, 1.25)

    QQC2.Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        font.pixelSize: 11
        font.bold: true
        elide: Text.ElideRight
        text: root.title
        color: "#111"
    }

    QQC2.Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 22
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        font.pixelSize: 10
        elide: Text.ElideRight
        text: root.subtitle
        color: "#232323"
        visible: subtitle.length > 0
    }
}
